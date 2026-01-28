#!/usr/bin/env python3
"""Build a TTF icon font from SVGs using FontForge.

Requires FontForge with Python bindings:
  - Windows: install FontForge and ensure the `fontforge` module is on PYTHONPATH.

Example:
  python build_icon_font.py --output OxygenIcons.ttf --font-name OxygenIcons
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

try:
    import fontforge  # type: ignore
except Exception as exc:  # pragma: no cover - environment dependent
    print(
        "FontForge Python module is required. Install FontForge and make sure "
        "the `fontforge` module is available on PYTHONPATH.\n"
        f"Import error: {exc}",
        file=sys.stderr,
    )
    sys.exit(1)


def _glyph_name_from_file(path: Path) -> str:
    return path.stem.replace(" ", "_").replace("-", "_")


def _trim_unused_glyphs(font: "fontforge.font", keep_names: set[str]) -> None:
    for glyph in list(font.glyphs()):
        if glyph.glyphname in keep_names:
            continue
        try:
            font.removeGlyph(glyph)
        except Exception:
            try:
                del font[glyph.encoding]
            except Exception:
                pass


def _to_upper_camel(name: str) -> str:
    parts = re.split(r"[^0-9a-zA-Z]+", name)
    cleaned_parts = [part for part in parts if part]
    if not cleaned_parts:
        return "Icon"
    return "".join(part[:1].upper() + part[1:] for part in cleaned_parts)


def _c_identifier(name: str, prefix: str) -> str:
    camel = _to_upper_camel(name)
    if camel[0].isdigit():
        camel = f"_{camel}"
    return f"{prefix}{camel}"


def _generate_header(
    header_out: Path,
    header_namespace: str,
    header_prefix: str,
    mapping: dict[str, int],
    font_name: str,
) -> None:
    header_out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "//===----------------------------------------------------------------------===//",
        "// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or",
        "// copy at https://opensource.org/licenses/BSD-3-Clause.",
        "// SPDX-License-Identifier: BSD-3-Clause",
        "//===----------------------------------------------------------------------===//",
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        f"// Auto-generated icon constants for {font_name}.",
        "",
        f"namespace {header_namespace} {{",
        "",
    ]

    for glyph_name in sorted(mapping.keys()):
        ident = _c_identifier(glyph_name, header_prefix)
        codepoint = mapping[glyph_name]
        codepoint_ident = f"{ident}Codepoint"
        utf8_bytes = chr(codepoint).encode("utf-8")
        utf8_literal = "".join(f"\\x{byte:02X}" for byte in utf8_bytes)
        lines.append(
            f"inline constexpr char32_t {codepoint_ident} = 0x{codepoint:04X};"
        )
        lines.append(
            f"inline constexpr std::string_view {ident} = " f'"{utf8_literal}";'
        )

    lines.append("")
    lines.append(f"}} // namespace {header_namespace}")
    header_out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _generate_embed_header(
    header_out: Path,
    header_namespace: str,
    font_name: str,
) -> None:
    header_out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "//===----------------------------------------------------------------------===//",
        "// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or",
        "// copy at https://opensource.org/licenses/BSD-3-Clause.",
        "// SPDX-License-Identifier: BSD-3-Clause",
        "//===----------------------------------------------------------------------===//",
        "",
        "#pragma once",
        "",
        f"namespace {header_namespace} {{",
        "",
        f"extern const unsigned int {font_name}_compressed_size;",
        f"extern const unsigned char {font_name}_compressed_data[];",
        "",
        f"}} // namespace {header_namespace}",
    ]
    header_out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _generate_embed_cpp(
    cpp_out: Path,
    header_include: str,
    header_namespace: str,
    tool_output: str,
) -> None:
    cpp_out.parent.mkdir(parents=True, exist_ok=True)
    output_lines = tool_output.splitlines()
    comment_lines: list[str] = []
    body_lines: list[str] = []
    in_comment_block = True
    for line in output_lines:
        if in_comment_block and line.startswith("//"):
            comment_lines.append(line)
            continue
        if in_comment_block and not line.strip():
            continue
        in_comment_block = False
        body_lines.append(line)

    lines = [
        "//===----------------------------------------------------------------------===//",
        "// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or",
        "// copy at https://opensource.org/licenses/BSD-3-Clause.",
        "// SPDX-License-Identifier: BSD-3-Clause",
        "//===----------------------------------------------------------------------===//",
        "",
    ]

    if comment_lines:
        lines.extend(comment_lines)
        lines.append("")

    lines.extend(
        [
            f"#include <{header_include}>",
            "",
            f"namespace {header_namespace} {{",
            "",
            "// clang-format off",
        ]
    )

    lines.extend(body_lines)
    if body_lines and body_lines[-1].strip():
        lines.append("")

    lines.extend(
        [
            "// clang-format on",
            "",
            f"}} // namespace {header_namespace}",
        ]
    )
    cpp_out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _compact_like_trim(font: "fontforge.font") -> None:
    try:
        font.selection.all()
        font.selection.invert()
        font.clear()
    except Exception:
        return


def _cleanup_glyph(glyph: "fontforge.glyph") -> None:
    for op in (
        "simplify",
        "correctDirection",
        "removeOverlap",
        "addExtrema",
        "round",
        "closeContours",
    ):
        method = getattr(glyph, op, None)
        if method is None:
            continue
        try:
            if op == "addExtrema":
                method("all")
            else:
                method()
        except Exception:
            continue


def _generate_font(
    font: "fontforge.font", output_file: Path, no_hints: bool
) -> None:
    output_file.parent.mkdir(parents=True, exist_ok=True)
    if no_hints:
        try:
            font.generate(
                str(output_file), flags=("omit-instructions", "no-hints")
            )
            return
        except Exception:
            try:
                font.generate(str(output_file), flags=("omit-instructions",))
                return
            except Exception:
                pass
    font.generate(str(output_file))


def build_font(
    input_dir: Path,
    output_file: Path,
    font_name: str,
    start_codepoint: int,
    mapping_out: Path | None,
    trim_unused: bool,
    keep_notdef: bool,
    cleanup_glyphs: bool,
    no_hints: bool,
    header_out: Path | None,
    header_namespace: str,
    header_prefix: str,
    embed_cpp_out: Path,
    embed_symbol: str,
    embed_header_out: Path,
    embed_namespace: str,
    embed_exe: Path,
) -> None:
    svg_files = sorted(input_dir.glob("*.svg"))
    if not svg_files:
        raise SystemExit(f"No SVG files found in {input_dir}")

    font = fontforge.font()
    font.fontname = font_name
    font.fullname = font_name
    font.familyname = font_name
    font.encoding = "UnicodeFull"
    font.em = 1000
    font.ascent = 800
    font.descent = 200

    codepoint = start_codepoint
    mapping: dict[str, int] = {}

    for svg_path in svg_files:
        glyph_name = _glyph_name_from_file(svg_path)
        glyph = font.createChar(-1, glyph_name)
        glyph.unicode = codepoint
        glyph.importOutlines(str(svg_path))
        if cleanup_glyphs:
            _cleanup_glyph(glyph)
        else:
            glyph.simplify()
            glyph.correctDirection()
            glyph.removeOverlap()
        glyph.width = font.em
        mapping[glyph_name] = codepoint
        codepoint += 1

    if trim_unused:
        keep = set(mapping.keys())
        if keep_notdef:
            keep.add(".notdef")
        _trim_unused_glyphs(font, keep)
        _compact_like_trim(font)

    _generate_font(font, output_file, no_hints)

    exe_path = embed_exe
    if not exe_path.is_absolute():
        exe_path = (Path(__file__).parent / exe_path).resolve()
    if not exe_path.exists():
        raise SystemExit(f"Embedding tool not found: {exe_path}")

    result = subprocess.run(
        [
            str(exe_path),
            "-u8",
            "-nostatic",
            str(output_file),
            embed_symbol,
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    _generate_embed_header(
        header_out=embed_header_out,
        header_namespace=embed_namespace,
        font_name=embed_symbol,
    )

    _generate_embed_cpp(
        cpp_out=embed_cpp_out,
        header_include=f"Oxygen/ImGui/Icons/{embed_header_out.name}",
        header_namespace=embed_namespace,
        tool_output=result.stdout,
    )

    if mapping_out is not None:
        mapping_out.parent.mkdir(parents=True, exist_ok=True)
        mapping_out.write_text(json.dumps(mapping, indent=2), encoding="utf-8")

    if header_out is not None:
        _generate_header(
            header_out=header_out,
            header_namespace=header_namespace,
            header_prefix=header_prefix,
            mapping=mapping,
            font_name=font_name,
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a TTF icon font from SVGs."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path(__file__).parent / "svg",
        help="Directory containing SVG files.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).parent / "OxygenIcons.ttf",
        help="Output TTF file path.",
    )
    parser.add_argument(
        "--font-name",
        default="OxygenIcons",
        help="Font name/family.",
    )
    parser.add_argument(
        "--start-codepoint",
        type=lambda v: int(v, 0),
        default=0xE000,
        help="Start codepoint (default: 0xE000).",
    )
    parser.add_argument(
        "--mapping-json",
        type=Path,
        default=Path(__file__).parent / "OxygenIcons.mapping.json",
        help="Optional output mapping JSON file.",
    )
    parser.add_argument(
        "--no-trim-unused",
        action="store_true",
        help="Do not trim unused glyphs from the generated font.",
    )
    parser.add_argument(
        "--keep-notdef",
        action="store_true",
        help="Keep the .notdef glyph in the generated font.",
    )
    parser.add_argument(
        "--no-cleanup",
        action="store_true",
        help="Skip extra glyph cleanup for problematic SVGs.",
    )
    parser.add_argument(
        "--no-hints",
        action="store_true",
        help="Generate the font without hinting instructions.",
    )
    parser.add_argument(
        "--header-out",
        type=Path,
        default=None,
        help="Output header file for icon constants.",
    )
    parser.add_argument(
        "--header-namespace",
        default="oxygen::imgui::icons",
        help="C++ namespace for generated constants.",
    )
    parser.add_argument(
        "--header-prefix",
        default="kIcon",
        help="Prefix for generated constant names.",
    )
    parser.add_argument(
        "--embed-cpp",
        type=Path,
        default=None,
        help="Output .cpp file with embedded font binary.",
    )
    parser.add_argument(
        "--embed-header",
        type=Path,
        default=None,
        help="Output .h file for embedded font binary.",
    )
    parser.add_argument(
        "--embed-symbol",
        default=None,
        help="C++ symbol base name for embedded font data.",
    )
    parser.add_argument(
        "--embed-namespace",
        default="oxygen::imgui::icons",
        help="C++ namespace for embedded font data.",
    )
    parser.add_argument(
        "--embed-exe",
        type=Path,
        default=Path(__file__).parent / "binary_to_compressed_c.exe",
        help="Path to binary_to_compressed_c.exe.",
    )

    args = parser.parse_args()
    header_out = (
        args.header_out
        if args.header_out is not None
        else args.output.parent / f"Icons{args.font_name}.h"
    )
    embed_cpp_out = (
        args.embed_cpp
        if args.embed_cpp is not None
        else args.output.parent / f"{args.font_name}.cpp"
    )
    embed_header_out = (
        args.embed_header
        if args.embed_header is not None
        else args.output.parent / f"{args.font_name}.h"
    )
    embed_symbol = args.embed_symbol or args.font_name

    build_font(
        input_dir=args.input_dir,
        output_file=args.output,
        font_name=args.font_name,
        start_codepoint=args.start_codepoint,
        mapping_out=args.mapping_json,
        trim_unused=not args.no_trim_unused,
        keep_notdef=args.keep_notdef,
        cleanup_glyphs=not args.no_cleanup,
        no_hints=args.no_hints,
        header_out=header_out,
        header_namespace=args.header_namespace,
        header_prefix=args.header_prefix,
        embed_cpp_out=embed_cpp_out,
        embed_symbol=embed_symbol,
        embed_header_out=embed_header_out,
        embed_namespace=args.embed_namespace,
        embed_exe=args.embed_exe,
    )


if __name__ == "__main__":
    main()
