"""Accounting checks independent of native timing or the current machine."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "exposure_cpu", Path(__file__).parents[1] / "AnalyzeExposureCpu.py")
cpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpu)


class ExposureCpuAccountingTests(unittest.TestCase):
    def test_nested_owners_are_counted_once(self):
        owners = cpu.merge([(10, 60), (20, 40), (50, 70), (90, 100)])
        self.assertEqual(owners, [(10, 70), (90, 100)])
        running = [(0, 30), (50, 95)]
        self.assertEqual(cpu.intersection(owners, running, [30, 95]), 45)

    def test_scheduler_migration_separates_blocked_and_ready_time(self):
        rows = [dict(qpc=0, cpu=0, old_thread=0, new_thread=7, old_state=1),
                dict(qpc=30, cpu=0, old_thread=7, new_thread=8, old_state=5),
                dict(qpc=50, cpu=1, old_thread=8, new_thread=7, old_state=1),
                dict(qpc=70, cpu=1, old_thread=7, new_thread=8, old_state=1),
                dict(qpc=90, cpu=0, old_thread=8, new_thread=7, old_state=1),
                dict(qpc=100, cpu=0, old_thread=7, new_thread=0, old_state=4)]
        running, blocked = cpu.schedule(rows, 7)
        self.assertEqual(running, [(0, 30), (50, 70), (90, 100)])
        self.assertEqual(blocked, [(30, 50)])
        self.assertEqual(cpu.intersection([(20, 95)], running, [30, 70, 100]), 35)
        self.assertEqual(cpu.intersection([(20, 95)], blocked, [50]), 20)

    def test_overlapping_thread_execution_is_rejected(self):
        rows = [dict(qpc=0, cpu=0, old_thread=0, new_thread=7, old_state=1),
                dict(qpc=10, cpu=1, old_thread=0, new_thread=7, old_state=1)]
        with self.assertRaises(AssertionError):
            cpu.schedule(rows, 7)

    def test_percentiles_preserve_slowest_frame_and_use_nearest_rank(self):
        self.assertEqual(cpu.distribution(range(1, 101)),
                         {"p50": 50, "p95": 95, "p99": 99, "max": 100})

    def test_nested_diagnostics_do_not_increase_owner_cpu(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "owners.csv").write_text(
                "frame_seq,thread_id,kind,start_qpc,end_qpc,label\n"
                "10,7,exposure,10,70,owner\n10,7,detail,20,40,nested\n")
            (root / "schedule.csv").write_text(
                "qpc,cpu,old_thread,new_thread,old_state\n"
                "0,0,0,7,1\n30,0,7,8,5\n50,0,8,7,1\n100,0,7,0,4\n")
            (root / "metadata.json").write_text(json.dumps(dict(
                status=0, clock_type=1, events_lost=0, buffers_lost=0,
                qpc_frequency=1_000_000, records=4)))
            (root / "manifest.json").write_text(json.dumps(dict(
                cpu_owner_timing=dict(complete=True, path="owners.csv", records=2,
                                      thread_id=7, qpc_frequency=1_000_000),
                first_frame_seq=10, last_frame_seq=10, sample_count=1,
                workload="synthetic", width=1920, view_count=2)))
            with contextlib.redirect_stdout(io.StringIO()):
                cpu.analyze(root / "manifest.json", root / "schedule.csv",
                            root / "metadata.json", root / "result.json")
            result = json.loads((root / "result.json").read_text())
            self.assertAlmostEqual(result["metrics_ms"]["active_ms"]["p95"], .04)
            self.assertAlmostEqual(result["nested"]["nested"]["active_ms"]["p95"], .01)


if __name__ == "__main__":
    unittest.main()
