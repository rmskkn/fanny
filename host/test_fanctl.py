"""Unit tests for fanctl.py. No hardware needed: serial.Serial is replaced
with a mock, same approach as test_fancontrold.py's fake FanController."""
import unittest
from unittest.mock import patch

import fanctl


class FanControllerCommandTest(unittest.TestCase):
    def _fc(self, reply_lines):
        """reply_lines: raw bytes fanctl's readline() returns, in order."""
        with patch("fanctl.serial.Serial") as serial_cls:
            ser = serial_cls.return_value
            ser.readline.side_effect = reply_lines
            fc = fanctl.FanController("/dev/fake")
        return fc, ser

    def test_writes_line_with_newline(self):
        fc, ser = self._fc([b"ok\n"])
        fc.command("ping")
        ser.write.assert_called_once_with(b"ping\n")

    def test_ok_with_no_body_returns_empty_list(self):
        fc, _ = self._fc([b"ok\n"])
        self.assertEqual(fc.command("ping"), [])

    def test_collects_body_lines_before_ok(self):
        fc, _ = self._fc([b"fan 1 mode=pwm duty=40\n", b"ain 1 raw=2048 mv=1650\n", b"ok\n"])
        self.assertEqual(fc.command("get"), ["fan 1 mode=pwm duty=40", "ain 1 raw=2048 mv=1650"])

    def test_err_raises_runtime_error_with_full_text(self):
        fc, _ = self._fc([b"err duty must be 0-100\n"])
        with self.assertRaisesRegex(RuntimeError, "err duty must be 0-100"):
            fc.command("set 1 200")

    def test_err_after_body_lines_still_raises(self):
        fc, _ = self._fc([b"fan 1 mode=pwm\n", b"err too many points\n"])
        with self.assertRaises(RuntimeError):
            fc.command("curve 1 1:1,2:2,3:3,4:4,5:5,6:6,7:7")

    def test_empty_read_raises_timeout(self):
        fc, _ = self._fc([b""])  # pyserial returns b"" on a read timeout
        with self.assertRaises(TimeoutError):
            fc.command("ping")


class FanControllerStatusTest(unittest.TestCase):
    def _status(self, body_lines):
        with patch("fanctl.serial.Serial") as serial_cls:
            serial_cls.return_value.readline.side_effect = body_lines + [b"ok\n"]
            fc = fanctl.FanController("/dev/fake")
        return fc.status()

    def test_indexed_fan_line(self):
        status = self._status([b"fan 1 mode=pwm duty=40 rpm=1200 stall=0 curve=ok\n"])
        fan1 = status["fan"][1]
        self.assertEqual(fan1["mode"], "pwm")       # kept as string
        self.assertEqual(fan1["curve"], "ok")        # kept as string, not float("ok")
        self.assertEqual(fan1["duty"], 40.0)
        self.assertEqual(fan1["rpm"], 1200.0)
        self.assertEqual(fan1["stall"], 0.0)

    def test_indexed_ain_line(self):
        status = self._status([b"ain 4 raw=2051 mv=1653\n"])
        self.assertEqual(status["ain"][4], {"raw": 2051.0, "mv": 1653.0})

    def test_unindexed_line(self):
        status = self._status([b"v12 raw=2988 mv=12050\n"])
        self.assertEqual(status["v12"], {"raw": 2988.0, "mv": 12050.0})

    def test_multiple_fan_indices_kept_separate(self):
        status = self._status([
            b"fan 1 mode=pwm duty=0 rpm=1200 stall=0 curve=none\n",
            b"fan 2 mode=dc duty=70 rpm=1260 stall=1 curve=bad\n",
        ])
        self.assertEqual(status["fan"][1]["curve"], "none")
        self.assertEqual(status["fan"][2]["curve"], "bad")
        self.assertEqual(status["fan"][2]["duty"], 70.0)

    def test_bare_flag_line_parses_into_key_value(self):
        # "failsafe=0" and "auto=0" have no space: one word, no name/index
        # prefix like "fan 1 ..." or "v12 ..." get. status() must still
        # split name from value instead of using the whole word as the key.
        status = self._status([b"failsafe=0\n", b"auto=1\n"])
        self.assertEqual(status["failsafe"], 0.0)
        self.assertEqual(status["auto"], 1.0)


class MainTest(unittest.TestCase):
    def _run(self, argv):
        with patch("fanctl.FanController") as fc_cls:
            fc = fc_cls.return_value
            with patch("sys.argv", ["fanctl.py", *argv]):
                fanctl.main()
            return fc

    def test_set_dispatches_command(self):
        fc = self._run(["set", "2", "40"])
        fc.command.assert_called_once_with("set 2 40")

    def test_set_all_dispatches_command(self):
        fc = self._run(["set", "all", "60"])
        fc.command.assert_called_once_with("set all 60")

    def test_mode_dispatches_command(self):
        fc = self._run(["mode", "3", "dc"])
        fc.command.assert_called_once_with("mode 3 dc")

    def test_auto_dispatches_command(self):
        fc = self._run(["auto", "on"])
        fc.command.assert_called_once_with("auto on")

    def test_get_prints_joined_lines(self):
        with patch("fanctl.FanController") as fc_cls:
            fc_cls.return_value.command.return_value = ["fan 1 mode=pwm", "ain 1 raw=2048 mv=1650"]
            with patch("sys.argv", ["fanctl.py", "get"]), patch("builtins.print") as mock_print:
                fanctl.main()
            mock_print.assert_called_once_with("fan 1 mode=pwm\nain 1 raw=2048 mv=1650")

    def test_runtime_error_exits_with_board_message(self):
        with patch("fanctl.FanController") as fc_cls:
            fc_cls.return_value.command.side_effect = RuntimeError("err duty must be 0-100")
            with patch("sys.argv", ["fanctl.py", "set", "1", "200"]):
                with self.assertRaises(SystemExit) as cm:
                    fanctl.main()
            self.assertEqual(str(cm.exception), "err duty must be 0-100")

    def test_keyboard_interrupt_during_watch_exits_cleanly(self):
        with patch("fanctl.FanController") as fc_cls:
            fc_cls.return_value.command.side_effect = KeyboardInterrupt
            with patch("sys.argv", ["fanctl.py", "watch"]):
                fanctl.main()  # must not raise


if __name__ == "__main__":
    unittest.main()
