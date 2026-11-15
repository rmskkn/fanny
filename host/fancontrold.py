#!/usr/bin/env python3
"""Temperature-based fan control daemon for the STM32 PWM fan controller.

Reads a config file that sets each fan channel to either a constant duty, a
temperature->duty curve driven by one of the board's AIN inputs, or a curve
driven by an NVIDIA GPU's temperature (via `nvidia-smi`). Polls the board
(`get`) and re-sends each channel's duty on every cycle, which doubles as
the ping that keeps the firmware's 20 s failsafe from tripping.

Example config (INI), see fancontrol.conf.example:

    [daemon]
    interval = 2.0

    [fan1]
    mode = curve
    sensor = 1            # AIN1
    curve = 30:20, 40:35, 50:55, 60:75, 70:100

    [fan2]
    mode = curve
    sensor = gpu0         # nvidia-smi GPU index 0
    curve = 40:20, 55:40, 65:65, 75:100

    [fan3]
    mode = constant
    duty = 40

Channels with no section are left alone (not pinged, not overridden). Add
`type = dc` to a `[fanN]` section for a 3-pin fan on a motor-switch channel;
it defaults to `type = pwm` (4-pin). The daemon sends `mode` once at startup
and again after any reconnect, since a firmware reset returns every channel
to `pwm`. It also queries each AIN-driven curve channel's table on the
board (`curve ch`, no points) and, only if that differs from the config,
replaces it over `curve`. The firmware keeps the table in flash for its
own `auto on` mode, so that channel keeps running its curve if the host
is ever unreachable; comparing first avoids rewriting flash on every
reconnect when nothing actually changed.
"""
import argparse
import configparser
import math
import os
import subprocess
import sys
import time

from fanctl import DEFAULT_PORT, FanController

FAN_COUNT = 4
AIN_COUNT = 4
CONFIG_DEFAULT_PATH = "~/.config/fancontrol.conf"

# Must match firmware's App/autocurve.c (MAX_CURVE_POINTS) and
# App/protocol.c (LINE_MAX), which bound what the `curve` command accepts.
CURVE_MAX_POINTS = 6
CURVE_LINE_MAX = 64


class ConfigError(Exception):
    pass


class ConstantChannel:
    needs_gpu = False

    def __init__(self, duty):
        self.duty = duty

    def target_duty(self, readings):
        return self.duty


class CurveChannel:
    """Shared curve interpolation; subclasses supply temperature_c(readings)."""

    needs_gpu = False

    def __init__(self, points):
        self.points = points  # sorted [(temp_c, duty), ...]

    def target_duty(self, readings):
        temp = self.temperature_c(readings)
        points = self.points
        if temp <= points[0][0]:
            return points[0][1]
        if temp >= points[-1][0]:
            return points[-1][1]
        for (t0, d0), (t1, d1) in zip(points, points[1:]):
            if t0 <= temp <= t1:
                frac = (temp - t0) / (t1 - t0)
                return round(d0 + frac * (d1 - d0))
        return points[-1][1]  # unreachable, keeps type checkers happy


class NtcCurveChannel(CurveChannel):
    def __init__(self, sensor, points, ntc_r25, ntc_beta, t25):
        super().__init__(points)
        self.sensor = sensor
        self.ntc_r25 = ntc_r25
        self.ntc_beta = ntc_beta
        self.t25 = t25 + 273.15

    def temperature_c(self, readings):
        status = readings["status"]
        mv = status["ain"][self.sensor]["mv"]
        vdda_mv = status["vdda"]["mv"]
        r_ntc = self.ntc_r25 * mv / (vdda_mv - mv)
        inv_t = 1.0 / self.t25 + math.log(r_ntc / self.ntc_r25) / self.ntc_beta
        return 1.0 / inv_t - 273.15


class GpuCurveChannel(CurveChannel):
    needs_gpu = True

    def __init__(self, gpu_index, points):
        super().__init__(points)
        self.gpu_index = gpu_index

    def temperature_c(self, readings):
        try:
            return readings["gpu"][self.gpu_index]
        except KeyError:
            raise RuntimeError(f"nvidia-smi did not report GPU {self.gpu_index}")


def read_gpu_temps():
    """{gpu_index: max(core_temp_c, memory_temp_c)} from `nvidia-smi`.

    Memory (HBM on the V100) often runs hotter than the core, and the
    motherboard/BIOS fan curve cannot see GPU memory temperature at all.
    """
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=index,temperature.gpu,temperature.memory",
             "--format=csv,noheader,nounits"],
            encoding="utf-8", timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as e:
        raise RuntimeError(f"nvidia-smi: {e}") from e
    temps = {}
    for line in out.strip().splitlines():
        idx_s, core_s, mem_s = (p.strip() for p in line.split(","))
        readings = [int(v) for v in (core_s, mem_s) if v.isdigit()]
        if readings:
            temps[int(idx_s)] = max(readings)
    return temps


def parse_curve(text):
    points = []
    for pair in text.split(","):
        temp_s, _, duty_s = pair.strip().partition(":")
        if not duty_s:
            raise ConfigError(f"bad curve point {pair!r}, want TEMP:DUTY")
        points.append((float(temp_s), int(duty_s)))
    points.sort(key=lambda p: p[0])
    return points


def curve_command(ch, points):
    """Builds fan ch's "curve" command line, checked against the firmware's
    own limits so a bad config fails fast at startup instead of crash-looping
    the daemon every reconnect. Temps are rounded to the integers the
    firmware's parser takes; two points that round to the same or a falling
    temperature are also rejected, since the firmware requires each curve to
    be strictly ascending."""
    if len(points) > CURVE_MAX_POINTS:
        raise ConfigError(f"fan{ch} curve has {len(points)} points, firmware allows {CURVE_MAX_POINTS}")
    rounded = [(round(t), d) for t, d in points]
    for i, (temp, duty) in enumerate(rounded):
        if not 0 <= duty <= 100:
            raise ConfigError(f"fan{ch} curve point {temp}:{duty} has duty out of 0-100")
        if i > 0 and temp <= rounded[i - 1][0]:
            prev_temp, prev_duty = rounded[i - 1]
            raise ConfigError(
                f"fan{ch} curve points {prev_temp}:{prev_duty} and {temp}:{duty} round to the same "
                "or a falling temperature; firmware needs strictly rising degC"
            )
    line = "curve {} {}".format(ch, ",".join(f"{temp}:{duty}" for temp, duty in rounded))
    if len(line) > CURVE_LINE_MAX - 1:
        raise ConfigError(f"fan{ch} curve command is {len(line)} chars, firmware allows {CURVE_LINE_MAX - 1}")
    return line


def load_config(path):
    cfg = configparser.ConfigParser(inline_comment_prefixes="#")
    if not cfg.read(path):
        raise ConfigError(f"cannot read config file: {path}")

    interval = cfg.getfloat("daemon", "interval", fallback=2.0)
    port = cfg.get("daemon", "port", fallback=None)

    channels = {}
    for ch in range(1, FAN_COUNT + 1):
        section = f"fan{ch}"
        if not cfg.has_section(section):
            continue
        mode = cfg.get(section, "mode")
        if mode == "constant":
            channel = ConstantChannel(cfg.getint(section, "duty"))
        elif mode == "curve":
            sensor = cfg.get(section, "sensor")
            points = parse_curve(cfg.get(section, "curve"))
            if sensor.startswith("gpu"):
                if not sensor[3:].isdigit():
                    raise ConfigError(f"[{section}] sensor {sensor!r} must be 'gpuN'")
                channel = GpuCurveChannel(int(sensor[3:]), points)
            else:
                if not sensor.isdigit():
                    raise ConfigError(f"[{section}] sensor must be an AIN number or 'gpuN', got {sensor!r}")
                if not 1 <= int(sensor) <= AIN_COUNT:
                    raise ConfigError(f"[{section}] sensor must be AIN 1..{AIN_COUNT}, got {sensor!r}")
                channel = NtcCurveChannel(
                    int(sensor),
                    points,
                    ntc_r25=cfg.getfloat(section, "ntc_r25", fallback=10000.0),
                    ntc_beta=cfg.getfloat(section, "ntc_beta", fallback=3950.0),
                    t25=cfg.getfloat(section, "t25", fallback=25.0),
                )
                channel.curve_cmd = curve_command(ch, points)
        else:
            raise ConfigError(f"[{section}] mode must be 'constant' or 'curve', got {mode!r}")
        channel.fan_type = cfg.get(section, "type", fallback="pwm")
        if channel.fan_type not in ("pwm", "dc"):
            raise ConfigError(f"[{section}] type must be 'pwm' or 'dc', got {channel.fan_type!r}")
        channels[ch] = channel
    return port, interval, channels


def run(fc, interval, channels):
    needs_gpu = any(channel.needs_gpu for channel in channels.values())
    for ch, channel in channels.items():
        fc.command(f"mode {ch} {channel.fan_type}")
        if isinstance(channel, NtcCurveChannel):
            # The curve lives in flash and survives a reset, unlike mode, so
            # only rewrite it (flash wear) when the board's copy actually
            # differs from the config's, not on every reconnect. `curve ch`
            # with no points queries instead of setting; the response line
            # is formatted exactly like curve_cmd, so plain text compares.
            current = fc.command(f"curve {ch}")
            current_line = current[0] if current else None
            if current_line != channel.curve_cmd:
                try:
                    fc.command(channel.curve_cmd)
                except RuntimeError as e:
                    print(f"fancontrold: fan{ch} curve rejected: {e}", file=sys.stderr, flush=True)
    while True:
        readings = {"status": fc.status(), "gpu": read_gpu_temps() if needs_gpu else {}}
        for ch, channel in channels.items():
            duty = max(0, min(100, channel.target_duty(readings)))
            fc.command(f"set {ch} {duty}")
        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-c", "--config", default=CONFIG_DEFAULT_PATH)
    parser.add_argument("-p", "--port", default=None, help="overrides the config file's port")
    args = parser.parse_args()

    config_path = os.path.expanduser(args.config)

    try:
        cfg_port, interval, channels = load_config(config_path)
    except ConfigError as e:
        sys.exit(str(e))
    if not channels:
        sys.exit(f"{config_path}: no [fanN] section defines a managed channel")

    port = args.port or cfg_port or DEFAULT_PORT

    while True:
        try:
            fc = FanController(port)
            run(fc, interval, channels)
        except (OSError, TimeoutError, RuntimeError) as e:
            print(f"fancontrold: {e}, reconnecting in 5 s", file=sys.stderr, flush=True)
            time.sleep(5)
        except KeyboardInterrupt:
            return


if __name__ == "__main__":
    main()
