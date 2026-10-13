"""Keep every SDK install rule accounted for by the install-sdk target."""

from pathlib import Path
import re
import unittest


ENGINE = Path(__file__).resolve().parents[3]
HELPERS = ENGINE / "cmake" / "Install.cmake"
RAW_INSTALL = re.compile(r"^\s*install\s*\(", re.IGNORECASE)
MARKER = re.compile(r"^\s*#\s*install-sdk:\s*(static|generated\s+\S+)\s*$")
REQUIRES = re.compile(r"\boxygen_install_requires\s*\(([^)]*)\)")


def cmake_files():
    for pattern in ("CMakeLists.txt", "*.cmake"):
        for path in ENGINE.rglob(pattern):
            relative = path.relative_to(ENGINE).parts
            if "out" in relative[:1] or ".git" in relative or path == HELPERS:
                continue
            yield path


class InstallRegistrationTests(unittest.TestCase):
    """install-sdk builds only what the SDK installs, so a raw install() rule must
    say what it copies and, when the build produces it, name and register the
    producer: '# install-sdk: generated <target>'."""

    def test_every_raw_install_rule_is_marked_and_its_producer_registered(self):
        problems = []
        for path in cmake_files():
            lines = path.read_text(encoding="utf-8").splitlines()
            relative = path.relative_to(ENGINE)
            producers = set()
            for index, line in enumerate(lines):
                if not RAW_INSTALL.match(line):
                    continue
                marker = MARKER.match(lines[index - 1] if index else "")
                if not marker:
                    problems.append(
                        f"{relative}:{index + 1}: install() needs a "
                        "'# install-sdk: static' or "
                        "'# install-sdk: generated <target>' comment on the line above"
                    )
                elif marker.group(1).startswith("generated"):
                    producers.add(marker.group(1).split()[1])
            registered = {
                name
                for match in REQUIRES.finditer("\n".join(lines))
                for name in match.group(1).split()
            }
            for producer in sorted(producers - registered):
                problems.append(
                    f"{relative}: install() of generated files names producer "
                    f"{producer}, but oxygen_install_requires({producer}) is not "
                    "called in this file"
                )
        self.assertEqual(problems, [], "\n".join(problems))


if __name__ == "__main__":
    unittest.main()
