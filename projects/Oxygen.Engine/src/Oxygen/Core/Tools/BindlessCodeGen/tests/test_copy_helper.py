import unittest
import tempfile
from pathlib import Path
import sys
from importlib import reload
import shutil

# Import the module under test
import bindless_codegen._copy_helper as ch

reload(ch)


class TestCopyHelper(unittest.TestCase):
    def write_file(self, path: Path, content: bytes, encoding=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        if encoding:
            path.write_text(content.decode(encoding), encoding=encoding)
        else:
            path.write_bytes(content)

    def test_adjacent_timestamps(self):
        # timestamps adjacent to code tokens should be stripped correctly
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "a.h"
            dst_dir = tmp / "out"
            dst_dir.mkdir()
            # src has timestamp immediately before token
            self.write_file(
                src, b"/* Generated: 2025-08-28 08:22:07 */int x;\n"
            )
            self.write_file(
                dst_dir / "a.h", b"/* Generated: 2025-08-28 09:33:44 */int x;\n"
            )
            from io import StringIO
            from contextlib import redirect_stdout

            buf = StringIO()
            with redirect_stdout(buf):
                rc = ch.copy_single(src, dst_dir / "a.h", verbose=True)
            out = buf.getvalue()
            self.assertEqual(rc, 0)
            self.assertIn("Skipping (timestamp diff only)", out)

    def test_different_encodings(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "u8.h"
            dst = tmp / "u8_out.h"
            # write latin-1 content with accent
            src_text = "/* Generated: 2025-08-28 08:22:07 */\nint café;\n"
            dst_text = "/* Generated: 2025-08-28 09:33:44 */\nint café;\n"
            src.write_text(src_text, encoding="latin-1")
            dst.write_text(dst_text, encoding="latin-1")
            from io import StringIO
            from contextlib import redirect_stdout

            buf = StringIO()
            with redirect_stdout(buf):
                rc = ch.copy_single(src, dst, verbose=True)
            out = buf.getvalue()
            self.assertEqual(rc, 0)
            # should skip due to timestamp-only change
            self.assertIn("Skipping (timestamp diff only)", out)

    def test_large_file_performance(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "big.h"
            dst = tmp / "big_out.h"
            header = "/* Generated: 2025-08-28 08:22:07 */\n"
            body = "int x;\n" * 1000
            dst_header = "/* Generated: 2025-08-28 09:33:44 */\n"
            src.write_text(header + body, encoding="utf-8")
            dst.write_text(dst_header + body, encoding="utf-8")
            from io import StringIO
            from contextlib import redirect_stdout

            buf = StringIO()
            with redirect_stdout(buf):
                rc = ch.copy_single(src, dst, verbose=True)
            out = buf.getvalue()
            self.assertEqual(rc, 0)
            self.assertIn("Skipping (timestamp diff only)", out)

    def test_unchanged_noop(self):
        # When files are identical, copy_single should return 0 and be a no-op
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "same.h"
            dst = tmp / "same_out.h"
            content = "/* Generated: 2025-08-28 08:22:07 */\nint x;\n"
            src.write_text(content, encoding="utf-8")
            dst.write_text(content, encoding="utf-8")
            # capture verbose output to ensure reporting is accurate
            from io import StringIO
            from contextlib import redirect_stdout

            buf = StringIO()
            with redirect_stdout(buf):
                rc = ch.copy_single(src, dst, verbose=True)
            out = buf.getvalue()
            self.assertEqual(rc, 0)
            # verbose output should include 'Unchanged'
            self.assertIn("Unchanged", out)
            # destination should remain unchanged
            self.assertEqual(dst.read_text(encoding="utf-8"), content)

    def test_missing_source_fails(self):
        # If the source file is missing, copy_single should return non-zero
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "nope.h"
            dst = tmp / "out.h"
            from io import StringIO
            from contextlib import redirect_stdout

            buf = StringIO()
            with redirect_stdout(buf):
                rc = ch.copy_single(src, dst, verbose=True)
            out = buf.getvalue()
            self.assertNotEqual(rc, 0)
            # verbose output should indicate failure and mention 'missing source'
            self.assertIn("missing source", out.lower())

    def test_copy_failure_propagates(self):
        # Simulate shutil.copy2 raising an exception and ensure copy_single returns error
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "to_copy.h"
            dst = tmp / "dest.h"
            content = "/* Generated: 2025-08-28 08:22:07 */\nint x;\n"
            src.write_text(content, encoding="utf-8")

            original_copy2 = shutil.copy2
            try:

                def fail_copy(a, b, **kwargs):
                    raise IOError("simulated copy failure")

                shutil.copy2 = fail_copy
                from io import StringIO
                from contextlib import redirect_stdout

                buf = StringIO()
                with redirect_stdout(buf):
                    rc = ch.copy_single(src, dst, verbose=True)
                out = buf.getvalue()
                self.assertNotEqual(rc, 0)
                # verbose output should indicate Failed and include the simulated message
                self.assertIn("Failed", out)
                self.assertIn("simulated copy failure", out)
            finally:
                shutil.copy2 = original_copy2


if __name__ == "__main__":
    unittest.main()
