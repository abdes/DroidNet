# PakDump Tool

PakDump is a developer diagnostics and inspection utility for Oxygen Engine
`.pak` content archives. It opens a PAK file, validates header/footer
structure, enumerates asset directory entries, and (optionally) decodes and
prints resource & asset descriptor data for fast, human–readable analysis.

## Key Capabilities

- Header / footer introspection (format + content versions, file sizing)
- Asset directory listing (type, offsets, sizes, GUIDs)
- Per‑asset descriptor preview with optional hex dump
- Material asset deep inspection (all descriptor fields + shader references)
- Resource tables overview (buffers, textures) with optional element details
- Raw resource data preview (buffer / texture blob bytes, truncated)
- Verbose mode for extended metadata (GUID raw bytes, resource internals)
- Byte preview length control (`--max-data=`)

## When To Use

Use PakDump to:

- Verify packer output integrity during content pipeline development
- Debug missing or malformed assets in builds
- Inspect material shader linkage & variant flags
- Spot size anomalies (oversized resources or descriptors)
- Quickly sample raw resource payloads without a debugger

## Build

The tool is part of the Oxygen.Engine monorepo and is built by the normal
CMake + Conan workflow. After configuring the project, the produced executable
(target name `Oxygen.Content.PakDump`) will be placed with other tools.

_No extra third‑party dependencies beyond those already provided by the
engine (fmt, loguru, etc.)._

## Usage

```bash
Oxygen.Content.PakDump <pakfile> [options]
```

### Options

| Option | Effect |
|--------|--------|
| `--no-header` | Suppress PAK header section. |
| `--no-footer` | Suppress PAK footer section. |
| `--no-directory` | Suppress asset directory listing. |
| `--no-resources` | Suppress resource tables section. |
| `--show-data` | Include hex dump previews of buffer / texture raw data. |
| `--hex-dump-assets` | Hex dump full asset descriptor bytes. |
| `--verbose` | Enable extended detail (GUID raw bytes, resource fields, limited entries). |
| `--max-data=N` | Limit bytes shown in any data/descriptor dump (default 256). |

### Examples

Basic overview:

```bash
Oxygen.Content.PakDump game.pak
```

Verbose with resource data previews:

```bash
Oxygen.Content.PakDump game.pak --verbose --show-data
```

Inspect only assets (skip resources):

```bash
Oxygen.Content.PakDump game.pak --no-resources --hex-dump-assets
```

Full deep dive:

```bash
Oxygen.Content.PakDump game.pak --verbose --show-data --hex-dump-assets --max-data=1024
```

## Output Structure

Sections appear only if not suppressed:

1. `PAK FILE ANALYSIS` – File path + total size.
2. `PAK HEADER` – Magic confirmation, format/content version, header size.
3. `PAK FOOTER` – Asset count, footer size.
4. `RESOURCE TABLES` – Sub‑sections for each registered resource type
   (`BUFFER RESOURCES`, `TEXTURE RESOURCES`). Shows counts; in verbose mode
   prints up to first 20 entries (offsets, sizes, formats). Use `--show-data`
   for truncated hex of raw payloads.
5. `ASSET DIRECTORY` – Each asset: GUID, type (enum + numeric), descriptor &
   entry offsets/sizes. If `--hex-dump-assets` set, prints descriptor bytes
   (truncated by `--max-data`). Material assets (type=1) additionally dump all
   structured fields plus shader reference descriptors.
6. `ANALYSIS COMPLETE` – Terminator marker.

## Material Asset Detailing

Material descriptors receive special handling:

- All header + material-specific fields printed with labels
- Base color, normal scale, metalness, roughness, AO values
- Associated texture indices
- Shader stage bitmask drives enumeration of appended shader reference blocks
- Each shader reference shows its Unique ID & hash; optional hex dump

## Performance / Safety Notes

- Reads are bounded: only up to `--max-data` bytes are dumped per section.
- Verbose mode caps detailed resource iteration to first 20 entries per table.
- Failures to parse individual assets/resources are caught and reported; tool
  continues with next item.

## Extensibility

Code registers specialized dumpers for known asset/resource types. To extend:

1. Implement a new `AssetDumper` or `ResourceTableDumper` subclass.
2. Register it in the respective registry constructor.
3. Add option flags if new dump modes warrant user control.

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success. |
| `1` | Usage error (missing file) or CLI misuse. |
| `2` | Unhandled runtime error while opening or parsing the pak. |

## Future Improvements (Ideas)

- Additional asset type specific dumpers (geometry, textures, pipelines)
- JSON / machine-readable output mode
- Filtering by asset type or name pattern
- Stats summary (aggregate sizes per asset/resource type)
- Integrity checks (hash verification) & warnings

## License

Distributed under the 3-Clause BSD License (see repository LICENSE).
