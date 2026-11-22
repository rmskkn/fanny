# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for fancontrold.py. No hardware needed: run() is driven
against a fake FanController, same spirit as the firmware's host-side C
tests driving autocurve.c against fakes."""
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import fancontrold as fcd


class ParseCurveTest(unittest.TestCase):
    def test_parses_and_sorts(self):
        self.assertEqual(
            fcd.parse_curve("50:55, 30:20, 40:35"),
            [(30.0, 20), (40.0, 35), (50.0, 55)],
        )

    def test_bad_point_raises(self):
        with self.assertRaises(fcd.ConfigError):
            fcd.parse_curve("30-20")


class CurveCommandTest(unittest.TestCase):
    def test_valid_curve(self):
        line = fcd.curve_command(2, [(30, 20), (40, 35), (50, 55), (60, 75), (70, 100)])
        self.assertEqual(line, "curve 2 30:20,40:35,50:55,60:75,70:100")

    def test_too_many_points_rejected(self):
        with self.assertRaisesRegex(fcd.ConfigError, "firmware allows 6"):
            fcd.curve_command(1, [(i, 10) for i in range(fcd.CURVE_MAX_POINTS + 1)])

    def test_duty_out_of_range_rejected(self):
        with self.assertRaisesRegex(fcd.ConfigError, "duty out of 0-100"):
            fcd.curve_command(1, [(30, 150)])

    def test_rounding_collision_rejected(self):
        with self.assertRaisesRegex(fcd.ConfigError, "same or a falling temperature"):
            fcd.curve_command(1, [(30.2, 20), (30.4, 35)])

    def test_line_too_long_rejected(self):
        points = [(-100 + i * 40, 10 + i) for i in range(6)]  # 6 wide points
        with self.assertRaisesRegex(fcd.ConfigError, "firmware allows"):
            fcd.curve_command(1, points + [(300, 99)])  # 7th point also trips the count check first

    def test_negative_temp_formats_with_sign(self):
        self.assertEqual(fcd.curve_command(1, [(-5, 20), (10, 80)]), "curve 1 -5:20,10:80")


class ChannelTest(unittest.TestCase):
    def test_constant_channel_ignores_readings(self):
        channel = fcd.ConstantChannel(40)
        self.assertEqual(channel.target_duty(readings=None), 40)
        self.assertFalse(channel.needs_gpu)

    def test_ntc_curve_channel_clamps_and_interpolates(self):
        points = [(30, 20), (40, 35), (50, 55), (60, 75), (70, 100)]
        channel = fcd.NtcCurveChannel(1, points, ntc_r25=10000.0, ntc_beta=3950.0, t25=25.0)

        def readings_for(temp_c):
            # Inverts NtcCurveChannel.temperature_c() to get the mv that
            # yields temp_c, so the test drives the real formula both ways.
            t_kelvin = temp_c + 273.15
            inv_t25 = 1.0 / (25.0 + 273.15)
            r_ntc = 10000.0 * pow(2.718281828459045, (1.0 / t_kelvin - inv_t25) * 3950.0)
            vdda_mv = 3300.0
            mv = vdda_mv * r_ntc / (r_ntc + 10000.0)
            return {"status": {"ain": {1: {"mv": mv}}, "vdda": {"mv": vdda_mv}}}

        self.assertAlmostEqual(channel.target_duty(readings_for(20)), 20, delta=1)
        self.assertAlmostEqual(channel.target_duty(readings_for(50)), 55, delta=1)
        self.assertAlmostEqual(channel.target_duty(readings_for(80)), 100, delta=1)

    def test_gpu_curve_channel_reads_its_own_index(self):
        channel = fcd.GpuCurveChannel(0, [(40, 20), (75, 100)])
        self.assertTrue(channel.needs_gpu)
        self.assertEqual(channel.target_duty({"gpu": {0: 60}}), 66)

    def test_gpu_curve_channel_missing_reading_raises(self):
        channel = fcd.GpuCurveChannel(0, [(40, 20), (75, 100)])
        with self.assertRaises(RuntimeError):
            channel.target_duty({"gpu": {1: 60}})


class ReadGpuTempsTest(unittest.TestCase):
    def test_parses_max_of_core_and_memory(self):
        csv = "0, 55, 61\n1, 70, 68\n"
        with patch("fancontrold.subprocess.check_output", return_value=csv):
            self.assertEqual(fcd.read_gpu_temps(), {0: 61, 1: 70})

    def test_missing_nvidia_smi_raises_runtime_error(self):
        with patch("fancontrold.subprocess.check_output", side_effect=FileNotFoundError("no nvidia-smi")):
            with self.assertRaises(RuntimeError):
                fcd.read_gpu_temps()

    def test_subprocess_error_raises_runtime_error(self):
        err = subprocess.TimeoutExpired(cmd="nvidia-smi", timeout=10)
        with patch("fancontrold.subprocess.check_output", side_effect=err):
            with self.assertRaises(RuntimeError):
                fcd.read_gpu_temps()


class LoadConfigTest(unittest.TestCase):
    def _write(self, text):
        fd, path = tempfile.mkstemp(suffix=".conf")
        with os.fdopen(fd, "w") as f:
            f.write(text)
        self.addCleanup(os.unlink, path)
        return path

    def test_missing_file_raises(self):
        with self.assertRaises(fcd.ConfigError):
            fcd.load_config("/nonexistent/fancontrol.conf")

    def test_no_fan_sections_returns_empty_channels(self):
        path = self._write("[daemon]\ninterval = 2.0\n")
        port, interval, channels = fcd.load_config(path)
        self.assertIsNone(port)
        self.assertEqual(interval, 2.0)
        self.assertEqual(channels, {})

    def test_full_example_style_config(self):
        path = self._write(
            "[daemon]\ninterval = 1.5\n\n"
            "[fan1]\nmode = curve\nsensor = 1\ncurve = 30:20, 40:35, 50:55\n\n"
            "[fan2]\nmode = curve\nsensor = gpu0\ncurve = 40:20, 75:100\n\n"
            "[fan3]\nmode = constant\nduty = 40\n\n"
            "[fan4]\ntype = dc\nmode = curve\nsensor = 1\ncurve = 30:35, 65:100\n"
        )
        port, interval, channels = fcd.load_config(path)
        self.assertIsNone(port)
        self.assertEqual(interval, 1.5)
        self.assertEqual(set(channels), {1, 2, 3, 4})
        self.assertIsInstance(channels[1], fcd.NtcCurveChannel)
        self.assertEqual(channels[1].curve_cmd, "curve 1 30:20,40:35,50:55")
        self.assertEqual(channels[1].fan_type, "pwm")
        self.assertIsInstance(channels[2], fcd.GpuCurveChannel)
        self.assertFalse(hasattr(channels[2], "curve_cmd"))  # firmware can't run a GPU curve
        self.assertIsInstance(channels[3], fcd.ConstantChannel)
        self.assertEqual(channels[3].duty, 40)
        self.assertEqual(channels[4].fan_type, "dc")

    def test_bad_mode_raises(self):
        path = self._write("[fan1]\nmode = fast\n")
        with self.assertRaises(fcd.ConfigError):
            fcd.load_config(path)

    def test_bad_sensor_raises(self):
        path = self._write("[fan1]\nmode = curve\nsensor = 9\ncurve = 30:20\n")
        with self.assertRaises(fcd.ConfigError):
            fcd.load_config(path)

    def test_bad_type_raises(self):
        path = self._write("[fan1]\nmode = constant\nduty = 40\ntype = usb\n")
        with self.assertRaises(fcd.ConfigError):
            fcd.load_config(path)

    def test_oversized_curve_fails_fast_at_load(self):
        points = ",".join(f"{i}:10" for i in range(fcd.CURVE_MAX_POINTS + 1))
        path = self._write(f"[fan1]\nmode = curve\nsensor = 1\ncurve = {points}\n")
        with self.assertRaisesRegex(fcd.ConfigError, "firmware allows"):
            fcd.load_config(path)


class _StopTest(Exception):
    """Breaks run()'s infinite polling loop once the test has what it needs."""


class FakeFanController:
    """current_curve_by_ch: {ch: "curve ch ..."} mimics what the board would
    currently echo back to a bare "curve ch" query, i.e. its flash content
    before this run() call."""

    def __init__(self, current_curve_by_ch=None, reject_curve=False, stop_after_sets=1):
        self.current_curve = dict(current_curve_by_ch or {})
        self.reject_curve = reject_curve
        self.sent = []
        self._set_count = 0
        self._stop_after_sets = stop_after_sets

    def status(self):
        return {"ain": {1: {"mv": 1650.0}}, "vdda": {"mv": 3300.0}}

    def command(self, line):
        self.sent.append(line)
        parts = line.split(maxsplit=2)
        if parts[0] == "curve":
            ch = int(parts[1])
            if len(parts) == 2:  # query: no points arg
                return [self.current_curve.get(ch, f"curve {ch} none")]
            if self.reject_curve:
                raise RuntimeError("err too many points")
            self.current_curve[ch] = line  # set: the board now has exactly this line
            return []
        if line.startswith("set"):
            self._set_count += 1
            if self._set_count >= self._stop_after_sets:
                raise _StopTest
        return []


class RunTest(unittest.TestCase):
    def setUp(self):
        patcher = patch("fancontrold.time.sleep")
        patcher.start()
        self.addCleanup(patcher.stop)

    def _channel(self, points=((30, 20), (40, 35))):
        channel = fcd.NtcCurveChannel(1, list(points), ntc_r25=10000.0, ntc_beta=3950.0, t25=25.0)
        channel.fan_type = "pwm"
        channel.curve_cmd = fcd.curve_command(1, list(points))
        return channel

    def test_first_connect_pushes_mode_and_curve(self):
        # Fresh board, never configured: query answers "none", so it mismatches.
        fc = FakeFanController()
        channels = {1: self._channel()}
        with self.assertRaises(_StopTest):
            fcd.run(fc, 2.0, channels)
        self.assertIn("mode 1 pwm", fc.sent)
        self.assertIn("curve 1", fc.sent)  # queried first
        self.assertIn(channels[1].curve_cmd, fc.sent)  # then pushed
        self.assertEqual(fc.current_curve[1], channels[1].curve_cmd)

    def test_matching_curve_skips_rewrite(self):
        channel = self._channel()
        fc = FakeFanController(current_curve_by_ch={1: channel.curve_cmd})
        channels = {1: channel}
        with self.assertRaises(_StopTest):
            fcd.run(fc, 2.0, channels)
        self.assertIn("curve 1", fc.sent)  # still queried
        self.assertNotIn(channel.curve_cmd, fc.sent)  # but never resent

    def test_stale_curve_is_replaced(self):
        # Board has an old/different table (status could even read "ok" for
        # it); config changed, so the mismatch must still trigger a push.
        channel = self._channel()
        fc = FakeFanController(current_curve_by_ch={1: "curve 1 10:5,20:99"})
        channels = {1: channel}
        with self.assertRaises(_StopTest):
            fcd.run(fc, 2.0, channels)
        self.assertIn(channel.curve_cmd, fc.sent)
        self.assertEqual(fc.current_curve[1], channel.curve_cmd)

    def test_corrupted_curve_is_replaced(self):
        channel = self._channel()
        fc = FakeFanController(current_curve_by_ch={1: "curve 1 bad"})
        channels = {1: channel}
        with self.assertRaises(_StopTest):
            fcd.run(fc, 2.0, channels)
        self.assertIn(channel.curve_cmd, fc.sent)

    def test_rejected_curve_does_not_abort_run(self):
        fc = FakeFanController(reject_curve=True)
        channels = {1: self._channel()}
        with self.assertRaises(_StopTest):  # reaches the polling loop despite the rejection
            fcd.run(fc, 2.0, channels)
        self.assertNotIn(1, fc.current_curve)  # push never took
        self.assertTrue(any(s.startswith("set 1 ") for s in fc.sent))

    def test_set_duty_is_clamped_to_0_100(self):
        channel = fcd.ConstantChannel(150)  # misconfigured, out of range
        channel.fan_type = "pwm"
        fc = FakeFanController()
        with self.assertRaises(_StopTest):
            fcd.run(fc, 2.0, {1: channel})
        self.assertIn("set 1 100", fc.sent)

    def test_gpu_temps_fetched_only_when_needed(self):
        channel = self._channel()
        fc = FakeFanController(current_curve_by_ch={1: channel.curve_cmd})
        with patch("fancontrold.read_gpu_temps") as gpu_fn:
            with self.assertRaises(_StopTest):
                fcd.run(fc, 2.0, {1: channel})
            gpu_fn.assert_not_called()


if __name__ == "__main__":
    unittest.main()
