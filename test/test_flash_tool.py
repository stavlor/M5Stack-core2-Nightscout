"""Portable release flashing-tool checks; no compiler or device needed."""
import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("flash_tool", Path(__file__).resolve().parents[1] / "flash.py")
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)
discover_platformio = flash.platformio_command


class FlashToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "include").mkdir()
        (self.root / "platformio.ini").write_text("[env:core2]\n")
        (self.root / "include/config.example.h").write_text("example configuration\n")
        (self.root / "include/config.h").write_text("existing private settings\n")
        self.addCleanup(patch.stopall)
        patch.object(flash, "ROOT", self.root).start()
        patch.object(flash, "platformio_command", return_value=["pio-test"]).start()
        self.run = patch.object(flash.subprocess, "run", return_value=SimpleNamespace(returncode=0)).start()
        self.output = io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def test_first_run_creates_example_and_stops(self):
        (self.root / "include/config.h").unlink()
        self.assertEqual(flash.main([]), 2)
        self.assertEqual((self.root / "include/config.h").read_text(), "example configuration\n")
        self.run.assert_not_called()

    def test_init_keeps_existing_config(self):
        self.assertEqual(flash.main(["--init-config"]), 0)
        self.assertEqual((self.root / "include/config.h").read_text(), "existing private settings\n")
        self.run.assert_not_called()

    def test_default_builds_and_uploads(self):
        self.assertEqual(flash.main([]), 0)
        self.assertEqual(self.run.call_args.args[0][-2:], ["--target", "upload"])
        self.assertEqual(self.run.call_args.kwargs["cwd"], self.root)

    def test_build_only_never_uploads(self):
        flash.main(["--build-only"])
        self.assertNotIn("upload", self.run.call_args.args[0])

    def test_port_is_forwarded_as_one_argument(self):
        flash.main(["--port", "COM9"])
        self.assertEqual(self.run.call_args.args[0][-2:], ["--upload-port", "COM9"])
        self.assertNotIn("shell", self.run.call_args.kwargs)

    def test_list_ports_needs_no_config(self):
        (self.root / "include/config.h").unlink()
        flash.main(["--list-ports"])
        self.assertEqual(self.run.call_args.args[0], ["pio-test", "device", "list"])
        self.assertFalse((self.root / "include/config.h").exists())

    def test_monitor_port_and_baud(self):
        flash.main(["--monitor", "--port", "/dev/ttyUSB0"])
        self.assertEqual(self.run.call_args.args[0], ["pio-test", "device", "monitor", "--baud", "115200", "--port", "/dev/ttyUSB0"])

    def test_child_failure_is_preserved(self):
        self.run.return_value.returncode = 7
        self.assertEqual(flash.main([]), 7)

    def test_missing_platformio_does_not_run(self):
        with patch.object(flash, "platformio_command", return_value=None), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(flash.main([]), 2)
        self.run.assert_not_called()

    def test_start_failure_is_reported(self):
        self.run.side_effect = OSError("cannot execute")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(flash.main([]), 1)

    def test_wrong_project_location_is_rejected(self):
        (self.root / "platformio.ini").unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(flash.main([]), 2)
        self.run.assert_not_called()

    def test_discover_platformio_on_path(self):
        with patch.object(flash.shutil, "which", return_value="/tools/pio"):
            self.assertEqual(discover_platformio(), ["/tools/pio"])

    def test_discover_current_python_module(self):
        with patch.object(flash.shutil, "which", return_value=None), patch.object(flash.importlib.util, "find_spec", return_value=object()):
            self.assertEqual(discover_platformio(), [flash.sys.executable, "-m", "platformio"])

    def test_discover_vscode_platformio_environment(self):
        interpreter = self.root / ".platformio/penv/Scripts/python.exe"
        interpreter.parent.mkdir(parents=True)
        interpreter.touch()
        with patch.object(flash.shutil, "which", return_value=None), patch.object(flash.importlib.util, "find_spec", return_value=None), patch.object(flash.Path, "home", return_value=self.root):
            self.assertEqual(discover_platformio(), [str(interpreter), "-m", "platformio"])


if __name__ == "__main__":
    unittest.main()
