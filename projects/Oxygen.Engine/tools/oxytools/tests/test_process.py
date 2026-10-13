"""Windows job ownership of child processes."""

import os
import subprocess
import sys
import unittest

from oxytools.process import WindowsJob


@unittest.skipUnless(os.name == "nt", "Windows job objects")
class WindowsJobTests(unittest.TestCase):
    def test_a_child_that_exited_before_assignment_is_not_an_error(self):
        process = subprocess.Popen([sys.executable, "-I", "-c", "pass"])
        process.wait()
        job = WindowsJob(process)
        job.terminate()
        job.close()

    def test_a_running_child_is_owned_and_terminated(self):
        process = subprocess.Popen(
            [sys.executable, "-I", "-c", "import time; time.sleep(60)"]
        )
        job = WindowsJob(process)
        try:
            self.assertIsNone(process.poll())
            job.terminate()
            self.assertEqual(process.wait(timeout=10), 130)
        finally:
            job.close()
            process.kill()


if __name__ == "__main__":
    unittest.main()
