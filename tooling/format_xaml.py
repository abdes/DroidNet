"""Run XAML Styler while preserving the repository's UTF-8 and LF contract."""

import codecs
from pathlib import Path
import subprocess
import sys


def main(paths: list[str]) -> int:
    for name in paths:
        path = Path(name)
        original = path.read_bytes()
        try:
            original.decode("utf-8-sig")
        except UnicodeDecodeError:
            print(f"{name}: expected UTF-8 XAML; file left unchanged", file=sys.stderr)
            return 1

        result = subprocess.run(
            ["dotnet", "xstyler", "--write-to-stdout", "-f", str(path)],
            stdout=subprocess.PIPE,
            check=False,
        )
        if result.returncode:
            return result.returncode

        formatted = result.stdout.removeprefix(codecs.BOM_UTF8).replace(b"\r\n", b"\n")
        if original.startswith(codecs.BOM_UTF8):
            formatted = codecs.BOM_UTF8 + formatted
        if formatted != original:
            path.write_bytes(formatted)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
