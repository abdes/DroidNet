"""Replay lifetime and pre-UI bootstrap tests; no RenderDoc installation needed."""

import importlib.util
import os
import runpy
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("renderdoc_analysis_helpers", TOOLS / "renderdoc_ui_analysis.py")
HELPERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPERS)


class ReplayLifetimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.report = self.root / "report.txt"
        self.events = []

    def fake_api(self, open_status=0, shutdown_fails=False):
        events = self.events

        class Controller:
            def Shutdown(self):
                events.append("controller.shutdown")
                if shutdown_fails:
                    raise RuntimeError("controller shutdown failed")

        class Capture:
            def OpenFile(self, *args):
                events.append("capture.open")
                return SimpleNamespace(code=open_status)

            def LocalReplaySupport(self):
                return 1

            def OpenCapture(self, *args):
                events.append("controller.open")
                return SimpleNamespace(code=0), Controller()

            def Shutdown(self):
                events.append("capture.shutdown")

        return SimpleNamespace(OpenCaptureFile=Capture, ResultCode=SimpleNamespace(Succeeded=0),
                               ReplaySupport=SimpleNamespace(Supported=1), ReplayOptions=lambda: None)

    def run_analysis(self, callback, **options):
        with patch.object(HELPERS, "renderdoc_module", return_value=self.fake_api(**options)):
            return HELPERS.run_replay_script(self.root / "capture.rdc", self.report, callback)

    def test_success_closes_handles_before_final_success(self):
        def callback(controller, report, *paths):
            self.events.append("analysis")
            report.append("value=42")

        self.assertEqual(self.run_analysis(callback), 0)
        self.assertEqual(self.events, ["capture.open", "controller.open", "analysis",
                                       "controller.shutdown", "capture.shutdown"])
        self.assertIn("replay_handles_shutdown=true", self.report.read_text())
        self.assertTrue(self.report.read_text().startswith("analysis_result=success\n"))

    def test_callback_failure_still_closes_both_handles(self):
        def callback(*args):
            raise RuntimeError("analysis failed")

        self.assertEqual(self.run_analysis(callback), 1)
        self.assertEqual(self.events[-2:], ["controller.shutdown", "capture.shutdown"])
        self.assertIn("analysis_result=exception", self.report.read_text())
        self.assertNotIn("analysis_result=success", self.report.read_text())

    def test_cleanup_failure_cannot_report_success(self):
        self.assertEqual(self.run_analysis(lambda *args: None, shutdown_fails=True), 1)
        self.assertEqual(self.events[-1], "capture.shutdown")
        self.assertIn("controller shutdown failed", self.report.read_text())
        self.assertIn("analysis_result=exception", self.report.read_text())

    def test_open_failure_releases_capture_without_controller(self):
        self.assertEqual(self.run_analysis(lambda *args: self.fail("must not analyze"), open_status=1), 1)
        self.assertEqual(self.events, ["capture.open", "capture.shutdown"])

    def test_bootstrap_syntax_error_always_exits_before_ui(self):
        script = self.root / "invalid.py"
        script.write_text("def invalid(:\n", encoding="utf-8")
        with patch.dict(os.environ, {
            "OXYGEN_RENDERDOC_AUTOMATION_MODE": "replay",
            "OXYGEN_RENDERDOC_SCRIPT_PATH": str(script),
            "OXYGEN_RENDERDOC_REPORT_PATH": str(self.report),
        }):
            with self.assertRaises(SystemExit) as raised:
                runpy.run_path(str(TOOLS / "RenderDocAnalysisBootstrap.py"), run_name="__main__")
        self.assertEqual(raised.exception.code, 0)
        self.assertIn("SyntaxError", self.report.read_text())
        self.assertIn("analysis_result=exception", self.report.read_text())

    def test_bootstrap_rejects_analyzer_without_shared_completion(self):
        script = self.root / "no-report.py"
        script.write_text("pass\n", encoding="utf-8")
        with patch.dict(os.environ, {
            "OXYGEN_RENDERDOC_AUTOMATION_MODE": "replay",
            "OXYGEN_RENDERDOC_SCRIPT_PATH": str(script),
            "OXYGEN_RENDERDOC_REPORT_PATH": str(self.report),
        }):
            with self.assertRaises(SystemExit):
                runpy.run_path(str(TOOLS / "RenderDocAnalysisBootstrap.py"), run_name="__main__")
        self.assertIn("analysis_result=exception", self.report.read_text())


if __name__ == "__main__":
    unittest.main()
