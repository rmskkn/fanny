# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for fwtool.py. No hardware needed: serial.Serial is replaced
with a mock, same approach as test_fanctl.py."""
import hashlib
import itertools
import struct
import unittest
from unittest.mock import MagicMock, mock_open, patch

import serial as pyserial

import fwtool


def _serial_mock():
    """A MagicMock standing in for a pyserial Serial: real Serial objects
    are context managers whose __enter__ returns self, which MagicMock
    does not do by default."""
    ser = MagicMock()
    ser.__enter__.return_value = ser
    return ser


def _reader(data):
    """A MagicMock.read side_effect that serves `data` in order, like
    pyserial does, instead of all at once."""
    pos = 0

    def read(n):
        nonlocal pos
        chunk = data[pos:pos + n]
        pos += len(chunk)
        return chunk

    return read


def _wire_frame(type_, payload=b""):
    """The exact bytes send_frame() would write for one frame, built
    against a throwaway MagicMock so the test doesn't duplicate the
    framing math fwtool.py already has."""
    ser = MagicMock()
    fwtool.send_frame(ser, type_, payload)
    return ser.write.call_args[0][0]


class BuildImageTest(unittest.TestCase):
    def test_length_and_padding(self):
        bin_bytes = b"\x10\x20\x30" * 100
        image = fwtool.build_image(bin_bytes, version=1)
        self.assertEqual(len(image), fwtool.APP_SIZE)
        self.assertEqual(image[:len(bin_bytes)], bin_bytes)
        self.assertEqual(image[len(bin_bytes):fwtool.APP_IMAGE_LEN], b"\xFF" * (fwtool.APP_IMAGE_LEN - len(bin_bytes)))

    def test_too_big_raises(self):
        with self.assertRaises(ValueError):
            fwtool.build_image(b"\x00" * (fwtool.APP_IMAGE_LEN + 1), version=1)

    def test_descriptor_fields(self):
        image = fwtool.build_image(b"\xAB" * 10, version=7)
        body = image[fwtool.APP_IMAGE_LEN:fwtool.APP_IMAGE_LEN + 64]
        self.assertEqual(body[0:4], b"CKY2")
        self.assertEqual(body[4], 2)  # format
        self.assertEqual(body[5], 2)  # CHUNKY_SCHEME_UNSIGNED
        self.assertEqual(body[6:8], b"\x00\x00")  # reserved
        product_id, version, security_counter, image_len, geometry_id = struct.unpack_from("<IIIII", body, 8)
        self.assertEqual(product_id, fwtool.FANNY_PRODUCT_ID)
        self.assertEqual(version, 7)
        self.assertEqual(security_counter, 7)  # defaults to version, matches chunky's own convention
        self.assertEqual(image_len, fwtool.APP_IMAGE_LEN)
        self.assertEqual(geometry_id, fwtool.FANNY_GEOMETRY_ID)
        self.assertEqual(body[60:64], b"\x00\x00\x00\x00")  # reserved_end

    def test_security_counter_override(self):
        image = fwtool.build_image(b"\xAB" * 10, version=7, security_counter=3)
        body = image[fwtool.APP_IMAGE_LEN:fwtool.APP_IMAGE_LEN + 64]
        self.assertEqual(struct.unpack_from("<I", body, 16)[0], 3)

    def test_image_sha256_matches(self):
        bin_bytes = b"\xAB" * 10
        image = fwtool.build_image(bin_bytes, version=1)
        region = image[:fwtool.APP_IMAGE_LEN]
        body = image[fwtool.APP_IMAGE_LEN:fwtool.APP_IMAGE_LEN + 64]
        self.assertEqual(body[28:60], hashlib.sha256(region).digest())

    def test_seal_matches_domain_and_body(self):
        image = fwtool.build_image(b"\xAB" * 10, version=1)
        descriptor = image[fwtool.APP_IMAGE_LEN:]
        body, seal = descriptor[:64], descriptor[64:]
        self.assertEqual(seal[:32], hashlib.sha256(fwtool.SEAL_DOMAIN + body).digest())
        self.assertEqual(seal[32:], b"\x00" * 32)


class FrameTest(unittest.TestCase):
    def test_send_frame_bytes(self):
        ser = MagicMock()
        fwtool.send_frame(ser, fwtool.FU_DATA, b"hello")
        written = ser.write.call_args[0][0]
        self.assertEqual(written[0:2], fwtool.FU_MAGIC)
        self.assertEqual(written[2], fwtool.FU_DATA)
        self.assertEqual(written[3:5], (5).to_bytes(2, "little"))
        self.assertEqual(written[5:10], b"hello")
        self.assertEqual(struct.unpack("<I", written[10:14])[0], fwtool._frame_crc(fwtool.FU_DATA, b"hello"))

    def test_send_frame_empty_payload(self):
        ser = MagicMock()
        fwtool.send_frame(ser, fwtool.FU_BEGIN)
        written = ser.write.call_args[0][0]
        self.assertEqual(len(written), 9)  # magic(2) + hdr(3) + crc(4), no payload

    def test_recv_frame_round_trip(self):
        ser = MagicMock()
        ser.read.side_effect = _reader(_wire_frame(fwtool.FU_ACK))
        type_, payload = fwtool.recv_frame(ser)
        self.assertEqual(type_, fwtool.FU_ACK)
        self.assertEqual(payload, b"")

    def test_recv_frame_round_trip_with_payload(self):
        ser = MagicMock()
        ser.read.side_effect = _reader(_wire_frame(fwtool.FU_DATA, b"hello"))
        type_, payload = fwtool.recv_frame(ser)
        self.assertEqual(type_, fwtool.FU_DATA)
        self.assertEqual(payload, b"hello")

    def test_recv_frame_bad_crc_raises(self):
        ser = MagicMock()
        frame = bytearray(_wire_frame(fwtool.FU_NAK))
        frame[-1] ^= 0x01  # flip a bit of the CRC
        ser.read.side_effect = _reader(bytes(frame))
        with self.assertRaises(ValueError):
            fwtool.recv_frame(ser)

    def test_recv_frame_bad_magic_raises(self):
        ser = MagicMock()
        ser.read.side_effect = _reader(b"\x00" * 16)
        with self.assertRaises(ValueError):
            fwtool.recv_frame(ser)

    def test_recv_frame_timeout(self):
        ser = MagicMock()
        ser.read.return_value = b""  # nothing ever arrives
        with self.assertRaises(TimeoutError):
            fwtool.recv_frame(ser, timeout=0.05)


class FlashTest(unittest.TestCase):
    def _image(self):
        return fwtool.build_image(b"\xAB" * 10, version=1)

    def test_sends_update_then_frames_then_checks_ack(self):
        app_ser, boot_ser = _serial_mock(), _serial_mock()
        boot_ser.read.side_effect = _reader(_wire_frame(fwtool.FU_ACK))

        with patch("fwtool.serial.Serial", side_effect=[app_ser, boot_ser]) as serial_cls:
            fwtool.flash("/dev/fake", self._image(), reboot_timeout=1.0)

        app_ser.write.assert_called_once_with(b"update\n")
        self.assertEqual(serial_cls.call_count, 2)
        # one BEGIN frame plus a FU_DATA frame per FU_MAX_PAYLOAD chunk
        expected_frames = 1 + -(-fwtool.APP_SIZE // fwtool.FU_MAX_PAYLOAD)
        self.assertEqual(boot_ser.write.call_count, expected_frames)

    def test_raises_on_nak(self):
        app_ser, boot_ser = _serial_mock(), _serial_mock()
        boot_ser.read.side_effect = _reader(_wire_frame(fwtool.FU_NAK))

        with patch("fwtool.serial.Serial", side_effect=[app_ser, boot_ser]):
            with self.assertRaises(RuntimeError):
                fwtool.flash("/dev/fake", self._image(), reboot_timeout=1.0)

    def test_times_out_if_port_never_comes_back(self):
        app_ser = _serial_mock()
        # the retry loop calls serial.Serial() until reboot_timeout elapses;
        # with sleep mocked out it can run many times in that real-time window
        never_comes_back = itertools.chain([app_ser], itertools.repeat(pyserial.SerialException("busy")))
        with patch("fwtool.serial.Serial", side_effect=never_comes_back), \
             patch("fwtool.time.sleep"):
            with self.assertRaises(TimeoutError):
                fwtool.flash("/dev/fake", self._image(), reboot_timeout=0.01)


class MainTest(unittest.TestCase):
    def test_build_writes_sealed_image(self):
        handle = mock_open(read_data=b"\xAB" * 10)
        with patch("builtins.open", handle), \
             patch("sys.argv", ["fwtool.py", "build", "app.bin", "-v", "3", "-o", "out.img"]):
            fwtool.main()
        written = b"".join(c.args[0] for c in handle().write.call_args_list)
        self.assertEqual(len(written), fwtool.APP_SIZE)

    def test_flash_dispatches_with_built_image(self):
        handle = mock_open(read_data=b"\xAB" * 10)
        with patch("builtins.open", handle), \
             patch("fwtool.flash") as flash_fn, \
             patch("sys.argv", ["fwtool.py", "flash", "app.bin", "-v", "2"]):
            fwtool.main()
        flash_fn.assert_called_once()
        port, image = flash_fn.call_args[0]
        self.assertEqual(port, fwtool.DEFAULT_PORT)
        self.assertEqual(len(image), fwtool.APP_SIZE)

    def test_flash_nak_propagates_from_main(self):
        # main() itself doesn't catch anything -- that's the __main__
        # guard's job (sys.exit(str(e))), which calling main() directly
        # bypasses, same as every other test in this class.
        handle = mock_open(read_data=b"\xAB" * 10)
        with patch("builtins.open", handle), \
             patch("fwtool.flash", side_effect=RuntimeError("bootloader rejected the update (NAK)")), \
             patch("sys.argv", ["fwtool.py", "flash", "app.bin", "-v", "2"]):
            with self.assertRaisesRegex(RuntimeError, "NAK"):
                fwtool.main()


if __name__ == "__main__":
    unittest.main()
