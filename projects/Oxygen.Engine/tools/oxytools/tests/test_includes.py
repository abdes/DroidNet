"""Include spelling and complete-block formatting contracts."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

from oxyformat.engine import find_formatter
from oxytools.includes import prepare_includes


class IncludeSpellingTests(unittest.TestCase):
    def test_only_oxygen_directives_are_normalized(self):
        source = (
            b'\xef\xbb\xbf#include "Oxygen/Alpha.h" // keep comment\r\n'
            b'#include "local.h"\r\n#include <vector>\r\n'
            b'#if ENABLED\r\n# include "Oxygen/Beta.h"\r\n#endif\r\n'
        )
        normalized, ranges = prepare_includes(source)
        self.assertEqual(
            normalized,
            source.replace(b'"Oxygen/Alpha.h"', b"<Oxygen/Alpha.h>").replace(
                b'"Oxygen/Beta.h"', b"<Oxygen/Beta.h>"
            ),
        )
        self.assertEqual(len(normalized), len(source))
        self.assertEqual(len(ranges), 4)
        self.assertEqual(prepare_includes(normalized)[0], normalized)

    def test_comments_strings_and_macro_bodies_are_not_directives(self):
        source = (
            b'/*\n#include "Oxygen/Comment.h"\n*/\n'
            b'auto text = R"tag(\n#include "Oxygen/Raw.h"\n)tag";\n'
            b'// continued comment \\\n#include "Oxygen/Comment2.h"\n'
            b'#define EXAMPLE \\\n#include "Oxygen/Macro.h"\n'
            b'const char* quoted = "#include \\"Oxygen/Text.h\\"";\n'
            b'/* leading comment */ #include "Oxygen/Real.h"\n'
        )
        normalized, ranges = prepare_includes(source)
        self.assertEqual(
            normalized, source.replace(b'"Oxygen/Real.h"', b"<Oxygen/Real.h>")
        )
        self.assertEqual(len(ranges), 1)

    def test_spliced_directive_preserves_bytes_and_range_offsets(self):
        source = b'// comment\r\n#inc\\\r\nlude "Oxygen/Part.h"\r\n'
        normalized, ranges = prepare_includes(source)
        self.assertEqual(
            normalized, source.replace(b'"Oxygen/Part.h"', b"<Oxygen/Part.h>")
        )
        start, size = ranges[0]
        self.assertEqual(
            normalized[start : start + size], b"#inc\\\r\nlude <Oxygen/Part.h>"
        )

    def test_format_disabled_regions_are_preserved(self):
        source = (
            b'// clang-format off\n#include "Oxygen/Untouched.h"\n'
            b'// clang-format on\n#include "Oxygen/Formatted.h"\n'
            b'/* clang-format off */\n#include "Oxygen/AlsoUntouched.h"\n'
        )
        normalized, ranges = prepare_includes(source)
        self.assertEqual(
            normalized,
            source.replace(b'"Oxygen/Formatted.h"', b"<Oxygen/Formatted.h>"),
        )
        self.assertEqual(len(ranges), 1)


class RepositoryIncludeStyleTests(unittest.TestCase):
    def test_groups_and_alphabetical_order_include_the_matching_header(self):
        root = Path(__file__).resolve().parents[3]
        source = (
            b'#include "Oxygen/Zeta.h"\n#include <vector>\n'
            b"#include <fmt/format.h>\n#include <gtest/gtest.h>\n"
            b"#include <Windows.h>\n#include <thirdparty>\n"
            b"#include <Oxygen/Alpha.h>\n#include <array>\n#include <stdio.h>\n"
        )
        normalized, _ = prepare_includes(source)
        command = [
            find_formatter(None),
            f"--style=file:{root / '.clang-format'}",
            "--assume-filename=Zeta.cpp",
            "--Werror",
            "--fail-on-incomplete-format",
        ]
        formatted = subprocess.run(
            command, input=normalized, capture_output=True, check=True
        ).stdout
        self.assertEqual(
            formatted,
            b"#include <array>\n#include <stdio.h>\n#include <vector>\n\n"
            b"#include <Windows.h>\n#include <fmt/format.h>\n"
            b"#include <gtest/gtest.h>\n#include <thirdparty>\n\n"
            b"#include <Oxygen/Alpha.h>\n#include <Oxygen/Zeta.h>\n",
        )
        again = subprocess.run(
            command, input=formatted, capture_output=True, check=True
        ).stdout
        self.assertEqual(again, formatted)
