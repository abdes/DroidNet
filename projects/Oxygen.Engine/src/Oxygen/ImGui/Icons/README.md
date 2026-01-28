# Icon Font Builder

This folder contains a small script to build a compact TTF icon font from SVG files using FontForge.

## Prerequisites

- FontForge installed with Python bindings.
- Use `ffpython` (FontForge’s Python) to run the script.

## Quick start

1. Place your SVG icons in the svg subfolder (or point `--input-dir` elsewhere).
2. Run the script with FontForge’s Python:

```pwsh
ffpython build_icon_font.py --output OxygenIcons.ttf --font-name OxygenIcons
```

## Options

- `--input-dir <path>`: Directory containing SVG files (default: svg subfolder).
- `--output <path>`: Output TTF path.
- `--font-name <name>`: Font family/name (used for generated file names).
- `--start-codepoint <hex>`: Starting codepoint (default: 0xE000).
- `--mapping-json <path>`: Output JSON mapping of glyph name to codepoint.
- `--no-trim-unused`: Keep all non-icon glyphs (default: trimmed).
- `--keep-notdef`: Keep the `.notdef` glyph in the output.
- `--no-cleanup`: Skip extra cleanup for problematic SVGs.
- `--no-hints`: Generate without hinting instructions.
- `--header-out <path>`: Output header for icon constants (default: `Icons<FontName>.h`).
- `--header-namespace <ns>`: Namespace for icon constants (default: `oxygen::imgui::icons`).
- `--header-prefix <prefix>`: Prefix for icon constants (default: `kIcon`).
- `--embed-cpp <path>`: Output .cpp for embedded font binary (default: `<FontName>.cpp`).
- `--embed-header <path>`: Output .h for embedded font binary (default: `<FontName>.h`).
- `--embed-symbol <name>`: Base symbol name for embedded data (default: `<FontName>`).
- `--embed-namespace <ns>`: Namespace for embedded data (default: oxygen::imgui::icons).
- `--embed-exe <path>`: Path to binary_to_compressed_c.exe.

## Example (with cleanup + no hints)

```pwsh
ffpython build_icon_font.py \
  --output OxygenIcons.ttf \
  --font-name OxygenIcons \
  --no-hints

## Generated files

For a font name of OxygenIcons, the script generates:

- OxygenIcons.ttf (icon font)
- OxygenIcons.mapping.json (glyph name -> codepoint)
- IconsOxygenIcons.h (icon constants)
- OxygenIcons.h (embedded data externs)
- OxygenIcons.cpp (embedded data definition with clang-format off/on)

Icons<FontName>.h contains both:

- `kIconFooCodepoint` (char32_t codepoint)
- `kIconFoo` (UTF-8 string constant)

## Using Oxygen icons in ImGui

The ImGui module loads OxygenIcons as a dedicated 24px icon font. Use the
UTF-8 string constants from IconsOxygenIcons.h when rendering icons, e.g.:

- `oxygen::imgui::icons::kIconContentLoader`
- `oxygen::imgui::icons::kIconSettings`
```

## Notes

- The script uses UnicodeFull and applies compact-style cleanup by pruning
  non-icon glyphs.
- The cleanup step attempts to fix common SVG issues (open contours, wrong
  direction, etc.).
- If binary_to_compressed_c.exe needs to be regenerated after updating its
  source, build it with MSVC, for example:
  - `cl.exe binary_to_compressed_c.cpp_`
