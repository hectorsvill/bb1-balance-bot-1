"""test_cli.py - plan-only flash.py, deadband_test.py's prompt, and the shared --json flag.

No robot, no compile, no network. flash.py without --execute must only print a
plan. deadband_test.py must not sit on input() when stdin is not a terminal.
"""

import importlib.util
import json
import os
import subprocess
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import deadband_test  # noqa: E402
import drive_test  # noqa: E402
import flash  # noqa: E402


def run(script, args, env=None, stdin=None):
    """Run tools/<script>. stdin=None inherits; pass DEVNULL for a non-TTY."""
    e = os.environ.copy()
    e.pop("BB1_HOST", None)
    if env:
        e.update(env)
    return subprocess.run([sys.executable, str(TOOLS / script), *args], cwd=REPO,
                          capture_output=True, text=True, env=e, stdin=stdin, timeout=20)


def plan(args, env=None):
    res = run("flash.py", args, env=env)
    try:
        body = json.loads(res.stdout)
    except ValueError:
        raise AssertionError(f"flash.py {args}: not JSON (exit {res.returncode}):\n{res.stdout}\n{res.stderr}")
    return res.returncode, body, res.stderr


class FlashPlanTest(unittest.TestCase):
    def test_usb_plan_is_json_and_does_not_compile(self):
        build_id = REPO / "firmware" / "bb1" / "build_id.h"
        before = build_id.read_text() if build_id.exists() else None
        code, body, err = plan([])
        after = build_id.read_text() if build_id.exists() else None
        self.assertEqual(code, 0, err)
        self.assertEqual(before, after)
        self.assertNotIn("Compiling", res_text(body, err))
        self.assertTrue(body["ok"])
        self.assertTrue(body["plan"])
        self.assertFalse(body["execute"])
        self.assertEqual(body["action"], "flash")
        self.assertEqual(body["target"], "firmware/bb1")
        self.assertEqual(body["transport"], "usb")
        self.assertIsNone(body["host"])
        self.assertIsNone(body["port"])
        self.assertEqual(body["firmware_path"], "firmware/bb1")
        self.assertEqual(body["artifact"], "build/bb1/bb1.ino.uf2")
        self.assertTrue(body["lying_down_check"])
        self.assertEqual(body["warnings"], [])
        self.assertEqual(err.strip(), "")

    def test_ota_plan_uses_host_and_env(self):
        code, body, _err = plan(["--ota", "--host", "192.168.4.20"])
        self.assertEqual(code, 0)
        self.assertEqual(body["transport"], "ota")
        self.assertEqual(body["host"], "192.168.4.20")
        self.assertEqual(body["artifact"], "build/bb1/bb1.ino.bin")
        self.assertTrue(body["lying_down_check"])
        code, body, _err = plan(["--ota"], env={"BB1_HOST": "10.0.0.8"})
        self.assertEqual(body["host"], "10.0.0.8")
        code, body, _err = plan(["--ota"])
        self.assertEqual(body["host"], "bb1.local")

    def test_port_and_skip_compile_are_in_the_plan(self):
        code, body, _err = plan(["-p", "/dev/ttyTEST", "--skip-compile"])
        self.assertEqual(code, 0)
        self.assertEqual(body["port"], "/dev/ttyTEST")
        self.assertTrue(body["skip_compile"])
        self.assertTrue(body["lying_down_check"])

    def test_force_without_execute_is_usage_and_warns(self):
        code, body, _err = plan(["--force", "--ota"])
        self.assertEqual(code, 2)
        self.assertFalse(body["ok"])
        self.assertTrue(body["plan"])
        self.assertFalse(body["execute"])
        self.assertFalse(body["lying_down_check"])
        self.assertEqual(body["exit_code"], 2)
        text = body["error"] + " " + " ".join(body["warnings"])
        self.assertIn("lying-down safety check", text)
        self.assertIn("--execute", text)
        self.assertIn("--force", text)

    def test_execute_checks_lying_down_unless_force(self):
        with patch("flash.compile_firmware", return_value="test-build") as compile_fw, \
             patch("flash.find_port", return_value="/dev/ttyFAKE"), \
             patch("flash.check_lying_down") as check, \
             patch("flash.upload") as upload, \
             patch("flash.record_flash"), \
             patch("flash.show_status"):
            quiet = StringIO()
            with redirect_stdout(quiet), redirect_stderr(quiet):
                flash.main(["--execute"])
            check.assert_called_once_with("/dev/ttyFAKE")
            compile_fw.assert_called_once()
            upload.assert_called_once_with("/dev/ttyFAKE")
            check.reset_mock()
            upload.reset_mock()
            with redirect_stdout(quiet), redirect_stderr(quiet):
                flash.main(["--yes", "--force"])
            check.assert_not_called()
            upload.assert_called_once()
            self.assertIn("lying-down safety check", quiet.getvalue())


class DriveHostTest(unittest.TestCase):
    def test_host_defaults_to_bb1_host(self):
        with patch.dict(os.environ, {"BB1_HOST": "10.9.8.7"}):
            args = drive_test.parser().parse_args(["--list"])
        self.assertEqual(args.host, "10.9.8.7")
        with patch.dict(os.environ, {}, clear=True):
            args = drive_test.parser().parse_args(["--host", "1.2.3.4", "--list"])
        self.assertEqual(args.host, "1.2.3.4")
        with patch.dict(os.environ, {}, clear=True):
            args = drive_test.parser().parse_args(["--list"])
        self.assertEqual(args.host, "bb1.local")


class DeadbandPromptTest(unittest.TestCase):
    def test_not_a_tty_without_yes_exits(self):
        res = run("deadband_test.py", [], stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 2)
        self.assertIn("--yes", res.stderr)
        self.assertIn("terminal", res.stderr.lower())
        res = run("deadband_test.py", ["--json"], stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 2)
        body = json.loads(res.stdout)
        self.assertFalse(body["ok"])
        self.assertEqual(body["exit_code"], 2)
        self.assertIn("--yes", body["error"])
        self.assertIn("terminal", body["error"].lower())

    def test_yes_does_not_prompt(self):
        with patch("builtins.input", side_effect=AssertionError("prompted")):
            deadband_test.confirm_wheels(True, as_json=False)

    def test_tty_no_is_a_failed_check(self):
        with patch("sys.stdin.isatty", return_value=True), \
             patch("builtins.input", return_value="n"):
            with self.assertRaises(SystemExit) as caught:
                deadband_test.confirm_wheels(False, as_json=False)
        self.assertEqual(caught.exception.code, 1)


class JsonFlagTest(unittest.TestCase):
    def test_every_tool_accepts_json(self):
        scripts = sorted(TOOLS.glob("*.py"))
        self.assertGreater(len(scripts), 10)
        for path in scripts:
            if path.name == "cli_util.py":
                continue
            if path.name == "bb1_mcp.py" and importlib.util.find_spec("mcp") is None:
                continue   # needs the mcp package (Python 3.10+); CI's 3.9 job doesn't have it
            with self.subTest(path.name):
                res = run(path.name, ["--help"])
                self.assertEqual(res.returncode, 0, res.stderr)
                self.assertIn("--json", res.stdout)


def res_text(body, err):
    return json.dumps(body) + err


if __name__ == "__main__":
    unittest.main()
