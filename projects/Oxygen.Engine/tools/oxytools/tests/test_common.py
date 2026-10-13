"""File fingerprints reuse settled hashes without missing a change."""

from __future__ import annotations

import hashlib
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from oxytools.common import file_hash


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class FileHashTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name) / "a.h"

    def settle(self, seconds_ago: float) -> None:
        when = time.time() - seconds_ago
        os.utime(self.path, (when, when))

    def test_settled_file_is_read_once(self) -> None:
        self.path.write_bytes(b"one")
        self.settle(60)
        self.assertEqual(file_hash(self.path), sha256(b"one"))
        with patch.object(Path, "read_bytes", side_effect=AssertionError("re-read")):
            self.assertEqual(file_hash(self.path), sha256(b"one"))

    def test_rewritten_file_is_hashed_again(self) -> None:
        self.path.write_bytes(b"one")
        self.settle(60)
        file_hash(self.path)
        self.path.write_bytes(b"two")
        self.settle(30)
        self.assertEqual(file_hash(self.path), sha256(b"two"))

    def test_recent_file_is_reread_despite_matching_size_and_mtime(self) -> None:
        self.path.write_bytes(b"one")
        stat = self.path.stat()
        self.assertEqual(file_hash(self.path), sha256(b"one"))
        # A same-size rewrite inside the timestamp granularity.
        self.path.write_bytes(b"two")
        os.utime(self.path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        self.assertEqual(file_hash(self.path), sha256(b"two"))


if __name__ == "__main__":
    unittest.main()
