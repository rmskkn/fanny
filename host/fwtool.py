#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and flash a firmware update for the fan controller's USB-CDC
bootloader (firmware/Bootloader/update_recv.c).

Builds the format2-sealed image blob itself (SPEC.md 4.1/4.2, scheme 2
unsigned -- see firmware/Core/flash_layout.h and
firmware/Bootloader/chunky/lib/format2_image.c, which this reimplements
in Python to avoid a second native build; the byte layout and the
CRC-32/SHA-256 math must match those C sources exactly). Pure stdlib
plus pyserial, no chunky checkout needed at run time.

Examples:
    fwtool.py build firmware/build/fanny_app.bin -v 2 -o update.img
    fwtool.py flash firmware/build/fanny_app.bin -v 2

flash first sends "update" over the running app's ASCII protocol (same
as fanctl.py's command link) to reboot into the bootloader, then speaks
update_recv.c's own binary frame protocol to stream the image.
"""
import argparse
import hashlib
import struct
import sys
import time
import zlib

import serial

DEFAULT_PORT = "/dev/ttyACM0"

# firmware/Core/flash_layout.h
BOOTLOADER_SIZE = 20 * 1024
APP_SIZE = 43 * 1024
DESCRIPTOR_LEN = 128
APP_IMAGE_LEN = APP_SIZE - DESCRIPTOR_LEN
FANNY_PRODUCT_ID = 0x46414E31  # "FAN1"
FANNY_GEOMETRY_ID = 0x00000001

# chunky_format.h (rust/chunky_format/c/chunky_format.h)
DESCRIPTOR_BODY_LEN = 64
SEAL_DOMAIN = b"chunky image descriptor v2"
assert len(SEAL_DOMAIN) == 26

# Bootloader/update_recv.c's own frame format, not chunky's protocol.h
FU_MAGIC = bytes([0xA5, 0x5A])
FU_MAX_PAYLOAD = 256
FU_BEGIN, FU_DATA, FU_ACK, FU_NAK = 1, 2, 3, 4


def build_image(bin_bytes, version, security_counter=None):
    """Returns the APP_SIZE-byte blob update_recv.c expects to receive:
    bin, 0xFF-padded to APP_IMAGE_LEN, then the 128 B sealed descriptor."""
    if len(bin_bytes) > APP_IMAGE_LEN:
        raise ValueError(f"{len(bin_bytes)} B app exceeds {APP_IMAGE_LEN} B app region")
    if security_counter is None:
        security_counter = version  # matches chunky's server_session.c/flash_file.c convention

    image = bin_bytes + b"\xFF" * (APP_IMAGE_LEN - len(bin_bytes))
    image_sha256 = hashlib.sha256(image).digest()

    body = bytearray(DESCRIPTOR_BODY_LEN)
    body[0:4] = b"CKY2"
    body[4] = 2  # format
    body[5] = 2  # CHUNKY_SCHEME_UNSIGNED
    # offset 6, 2 B reserved: left zero
    struct.pack_into("<I", body, 8, FANNY_PRODUCT_ID)
    struct.pack_into("<I", body, 12, version)
    struct.pack_into("<I", body, 16, security_counter)
    struct.pack_into("<I", body, 20, APP_IMAGE_LEN)
    struct.pack_into("<I", body, 24, FANNY_GEOMETRY_ID)
    body[28:60] = image_sha256
    # offset 60, 4 B reserved_end: left zero

    seal_hash = hashlib.sha256(SEAL_DOMAIN + bytes(body)).digest()
    descriptor = bytes(body) + seal_hash + b"\x00" * 32
    assert len(descriptor) == DESCRIPTOR_LEN

    return image + descriptor


def _frame_crc(type_, payload):
    return zlib.crc32(bytes([type_, len(payload) & 0xFF, len(payload) >> 8]) + payload) & 0xFFFFFFFF


def send_frame(ser, type_, payload=b""):
    hdr = bytes([type_, len(payload) & 0xFF, len(payload) >> 8])
    crc = _frame_crc(type_, payload)
    ser.write(FU_MAGIC + hdr + payload + struct.pack("<I", crc))


def recv_frame(ser, timeout=5.0):
    """Blocks for one frame; raises TimeoutError or ValueError (bad CRC/magic)."""
    deadline = time.time() + timeout

    def read(n):
        buf = b""
        while len(buf) < n:
            if time.time() > deadline:
                raise TimeoutError("bootloader did not reply in time")
            chunk = ser.read(n - len(buf))
            if not chunk:
                continue
            buf += chunk
        return buf

    if read(1) != FU_MAGIC[0:1] or read(1) != FU_MAGIC[1:2]:
        raise ValueError("bad frame magic")
    hdr = read(3)
    type_, length = hdr[0], hdr[1] | (hdr[2] << 8)
    payload = read(length) if length else b""
    crc_got = struct.unpack("<I", read(4))[0]
    if crc_got != _frame_crc(type_, payload):
        raise ValueError("bad frame CRC")
    return type_, payload


def flash(port, image, reboot_timeout=5.0):
    with serial.Serial(port, 115200, timeout=1.0) as ser:
        ser.reset_input_buffer()
        ser.write(b"update\n")
        # The app resets into the bootloader on this command and never
        # replies "ok" (App/protocol.c) -- the CDC link itself drops and
        # the bootloader's own usb_cdc_init() brings it back on the same
        # device node, so just wait for the port to come back.
        time.sleep(0.3)

    deadline = time.time() + reboot_timeout
    ser = None
    while time.time() < deadline:
        try:
            ser = serial.Serial(port, 115200, timeout=1.0)
            break
        except serial.SerialException:
            time.sleep(0.2)
    if ser is None:
        raise TimeoutError(f"{port} did not come back after 'update'")

    with ser:
        send_frame(ser, FU_BEGIN)
        for off in range(0, len(image), FU_MAX_PAYLOAD):
            send_frame(ser, FU_DATA, image[off:off + FU_MAX_PAYLOAD])
        type_, _ = recv_frame(ser, timeout=10.0)
        if type_ != FU_ACK:
            raise RuntimeError("bootloader rejected the update (NAK)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", default=DEFAULT_PORT)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_build = sub.add_parser("build", help="write the sealed update image to a file")
    p_build.add_argument("bin", help="raw app binary, e.g. firmware/build/fanny_app.bin")
    p_build.add_argument("-v", "--version", type=int, required=True)
    p_build.add_argument("-o", "--out", required=True)

    p_flash = sub.add_parser("flash", help="build and stream the update over USB CDC")
    p_flash.add_argument("bin", help="raw app binary, e.g. firmware/build/fanny_app.bin")
    p_flash.add_argument("-v", "--version", type=int, required=True)

    args = parser.parse_args()

    with open(args.bin, "rb") as f:
        bin_bytes = f.read()

    if args.cmd == "build":
        image = build_image(bin_bytes, args.version)
        with open(args.out, "wb") as f:
            f.write(image)
        print(f"wrote {len(image)} B to {args.out}")
    elif args.cmd == "flash":
        image = build_image(bin_bytes, args.version)
        flash(args.port, image)
        print("update flashed and verified")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, TimeoutError, OSError) as e:
        sys.exit(str(e))
