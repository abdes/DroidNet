"""Accounting checks independent of native timing or the current machine."""
import importlib.util
from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
