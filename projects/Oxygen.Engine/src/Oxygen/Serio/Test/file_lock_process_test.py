"""Exercise native file leases across independent processes."""

from contextlib import contextmanager
from pathlib import Path
import subprocess
import sys
import tempfile


@contextmanager
def hold(probe: Path, marker: Path, mode: str):
    process = subprocess.Popen(
        [str(probe), str(marker), mode, "hold"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True,
    )
    try:
        assert process.stdout.readline().strip() == "acquired"
        yield
    finally:
        process.communicate("release\n", timeout=10)
        assert process.returncode == 0


def check(probe: Path, marker: Path, mode: str, expected: str):
    result = subprocess.run(
        [str(probe), str(marker), mode, "try"],
        capture_output=True, text=True, timeout=10, check=True,
    )
    assert result.stdout.strip() == expected, result


def main():
    probe = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="oxygen-file-lock-") as directory:
        marker = Path(directory) / "generation.lock"
        marker.touch()
        with hold(probe, marker, "shared"):
            check(probe, marker, "shared", "acquired")
            check(probe, marker, "exclusive", "busy")
            with hold(probe, marker, "shared"):
                check(probe, marker, "exclusive", "busy")
        with hold(probe, marker, "exclusive"):
            check(probe, marker, "shared", "busy")
            check(probe, marker, "exclusive", "busy")
        check(probe, marker, "exclusive", "acquired")


if __name__ == "__main__":
    main()
