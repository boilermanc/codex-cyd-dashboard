import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "helper_diagnostics", Path(__file__).resolve().parents[1] / "scripts/helper_diagnostics.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.log = module.Diagnostics(self.temp.name)

    def entries(self):
        return [json.loads(line) for line in self.log.path.read_text(encoding="utf-8").splitlines()]

    def test_exception_records_codes_without_messages_or_payloads(self):
        error = OSError(5, "private chat and secret path")
        error.winerror = 6
        self.assertTrue(self.log.event("usb_error", error=error, title="secret", device_id="123456abcdef",
                                       path="private", traceback="private", payload={"secret": 1}, attempt=2))
        entry = self.entries()[0]
        self.assertEqual(entry["error_class"], "OSError")
        self.assertEqual(entry["errno"], 5)
        self.assertEqual(entry["winerror"], 6)
        self.assertEqual(entry["attempt"], 2)
        self.assertNotIn("secret", self.log.path.read_text(encoding="utf-8"))
        self.assertNotIn("private", self.log.path.read_text(encoding="utf-8"))

    def test_rejects_arbitrary_strings_nonfinite_and_wrong_types(self):
        self.assertFalse(self.log.event("private title"))
        self.assertTrue(self.log.event("heartbeat", duration_ms="secret", ack_age_ms=float("nan"),
                                       send_age_ms=float("inf"), connected="secret", attempt=True))
        self.assertEqual(set(self.entries()[0]), {"timestamp", "event", "pid"})

    def test_transition_logs_initial_and_changed_state_only(self):
        self.assertTrue(self.log.transition("acknowledged", False))
        self.assertTrue(self.log.transition("acknowledged", False))
        self.assertTrue(self.log.transition("acknowledged", True))
        self.assertFalse(self.log.transition("private chat", True))
        self.assertFalse(self.log.transition("acknowledged", "private"))
        self.assertEqual([entry["value"] for entry in self.entries()], [False, True])

    def test_rotates_to_three_bounded_files(self):
        self.log.MAX_BYTES = 180
        for attempt in range(20):
            self.assertTrue(self.log.event("usb_error", attempt=attempt))
        files = list(self.log.path.parent.glob("usb-helper-events.jsonl*"))
        self.assertEqual(len(files), 3)
        self.assertTrue(all(path.stat().st_size <= 180 for path in files))
        self.assertEqual(self.entries()[-1]["attempt"], 19)
        for path in files:
            for line in path.read_text(encoding="utf-8").splitlines():
                json.loads(line)

    def test_failed_write_does_not_raise_and_transition_retries(self):
        with patch.object(Path, "open", side_effect=PermissionError("private")):
            self.assertFalse(self.log.event("start"))
            self.assertFalse(self.log.transition("serial_open", True))
        self.assertTrue(self.log.transition("serial_open", True))
        self.assertEqual(len(self.entries()), 1)

    def test_failed_rotation_does_not_raise(self):
        self.assertTrue(self.log.event("start"))
        self.log.MAX_BYTES = 1
        with patch.object(Path, "replace", side_effect=PermissionError("private")):
            self.assertFalse(self.log.event("heartbeat"))


if __name__ == "__main__":
    unittest.main()
