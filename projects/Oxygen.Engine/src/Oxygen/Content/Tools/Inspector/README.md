# Oxygen.Content.Inspector

A developer diagnostics tool to validate and inspect **loose cooked** content
roots.

The tool loads and validates `container.index.bin` and prints a human-readable
summary of its contents.

## When to use

- Confirm a cooked root is structurally valid (index + referenced files).
- Quickly list asset entries and file records from `container.index.bin`.
- Debug SHA-256 digest mismatches reported by the Content module.

## Build

This tool is built as part of the main Oxygen.Engine CMake build.

- CMake build target: `Oxygen.Content.Inspector`
- Generated MSBuild target name (implementation detail):
  `oxygen-content-inspector`

Example:

```powershell
cmake --build out/build --config Debug --target "Oxygen.Content.Inspector"
```

The executable is produced under:

- `out/build/bin/<Config>/Oxygen.Content.Inspector.exe`

## Usage

Run `--help` for full CLI help:

```powershell
out/build/bin/Debug/Oxygen.Content.Inspector.exe --help
```

### Validate a cooked root

```powershell
Oxygen.Content.Inspector.exe validate-root <cooked_root>
```

Options:

- `-q`, `--quiet`: suppress success output

Example:

```powershell
Oxygen.Content.Inspector.exe validate-root F:/path/to/loose_cooked_root
```

### Dump the index

```powershell
Oxygen.Content.Inspector.exe dump-index <cooked_root> [--assets] [--files] [--digests]
```

Notes:

- If neither `--assets` nor `--files` is specified, both sections are printed.
- `--digests` includes SHA-256 values if present in the index.

Examples:

```powershell
# Dump everything (assets + file records)
Oxygen.Content.Inspector.exe dump-index F:/path/to/loose_cooked_root

# Dump only asset entries including descriptor digests
Oxygen.Content.Inspector.exe dump-index F:/path/to/loose_cooked_root --assets --digests
```

## Exit codes

- `0`: success
- `1`: CLI usage / unknown command
- `2`: validation or runtime error while loading/inspecting
- `3`: unexpected top-level failure

## Implementation notes

This tool intentionally depends only on exported module APIs:

- `oxygen::content::AssetLoader` for validation
- `oxygen::content::LooseCookedInspection` for inspection output

## License

Distributed under the 3-Clause BSD License (see repository LICENSE).
