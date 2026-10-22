#!/usr/bin/env python3
"""Temperature-based fan control daemon for the STM32 PWM fan controller.

Reads a config file that sets each fan channel to either a constant duty, a
temperature->duty curve driven by one of the board's AIN inputs, or a curve
driven by an NVIDIA GPU's temperature (via `nvidia-smi`). Polls the board
(`get`) and re-sends each channel's duty on every cycle, which doubles as
the ping that keeps the firmware's 10 s failsafe from tripping.

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

Channels with no section are left alone (not pinged, not overridden).
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
CONFIG_DEFAULT_PATH = "~/.config/fancontrol.conf"


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
            channels[ch] = ConstantChannel(cfg.getint(section, "duty"))
        elif mode == "curve":
            sensor = cfg.get(section, "sensor")
            points = parse_curve(cfg.get(section, "curve"))
            if sensor.startswith("gpu"):
                if not sensor[3:].isdigit():
                    raise ConfigError(f"[{section}] sensor {sensor!r} must be 'gpuN'")
                channels[ch] = GpuCurveChannel(int(sensor[3:]), points)
            else:
                if not sensor.isdigit():
                    raise ConfigError(f"[{section}] sensor must be an AIN number or 'gpuN', got {sensor!r}")
                channels[ch] = NtcCurveChannel(
                    int(sensor),
                    points,
                    ntc_r25=cfg.getfloat(section, "ntc_r25", fallback=10000.0),
                    ntc_beta=cfg.getfloat(section, "ntc_beta", fallback=3950.0),
                    t25=cfg.getfloat(section, "t25", fallback=25.0),
                )
        else:
            raise ConfigError(f"[{section}] mode must be 'constant' or 'curve', got {mode!r}")
    return port, interval, channels


def run(fc, interval, channels):
    needs_gpu = any(channel.needs_gpu for channel in channels.values())
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
