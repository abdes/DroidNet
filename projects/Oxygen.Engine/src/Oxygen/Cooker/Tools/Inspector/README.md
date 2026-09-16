# Oxygen.Cooker.Inspector

A developer diagnostics tool to validate and inspect **loose cooked** content
roots.

The tool loads and validates `container.index.bin` and prints a human-readable
summary of its contents.

## When to use

- Confirm a cooked root is structurally valid (index + referenced files).
- Quickly list asset entries and file records from `container.index.bin`.
- Confirm cooked scene descriptors (`AssetType::kScene`) are present and
  discoverable in a loose cooked root.
- Debug SHA-256 digest mismatches reported by the Content module.

## Build

This tool is built as part of the main Oxygen.Engine CMake build.

- CMake build target: `Oxygen.Cooker.Inspector`
- Generated MSBuild target name (implementation detail):
  `oxygen-content-inspector`

Example:

```powershell
cmake --build out/build --config Debug --target "Oxygen.Cooker.Inspector"
```

The executable is produced under:

- `out/build/bin/<Config>/Oxygen.Cooker.Inspector.exe`

## Usage

Run `--help` for full CLI help:

```powershell
out/build/bin/Debug/Oxygen.Cooker.Inspector.exe --help
```

### Validate a cooked root

```powershell
Oxygen.Cooker.Inspector.exe validate <cooked_root>
```

Options:

- `-q`, `--quiet`: suppress success output

Example:

```powershell
Oxygen.Cooker.Inspector.exe validate F:/path/to/loose_cooked_root
```

### Dump the index

```powershell
Oxygen.Cooker.Inspector.exe index <cooked_root> [--assets true] [--files true] [--digests true]
```

Notes:

- If neither `--assets` nor `--files` is specified, both sections are printed.
- `--digests` includes SHA-256 values if present in the index.
- Asset entries include the cooked `type` (e.g. `material`, `geometry`,
  `scene`).

Examples:

```powershell
# Dump everything (assets + file records)
Oxygen.Cooker.Inspector.exe index F:/path/to/loose_cooked_root

# Dump only asset entries including descriptor digests
Oxygen.Cooker.Inspector.exe index F:/path/to/loose_cooked_root --assets true --digests true
```

### Scene metadata and current-format validation

`validate` checks scene descriptors through the native parse-only loader in
addition to index, type and file-size checks. Retired scene versions, malformed
node records and invalid flag sources fail validation; content must be recooked.

```powershell
Oxygen.Cooker.Inspector.exe scenes <cooked_root> --output <scenes.json>
```

Writes `oxygen.cooked-scenes.v1` metadata with source identity, scene asset keys,
virtual paths, descriptor versions, and node identities/names/parent indices.
The three node flags report authored source choices: visibility is
`inherit|shown|hidden`; Cast/Receive Shadows are `inherit|on|off`. Root parents
refer to their own index. These are stored source modes, not runtime-effective
visibility, lighting eligibility, or rendered results.

Every scene is parsed with the native loader, without resource loading or renderer
startup. A malformed or incomplete scene adds a diagnostic scoped to its key/path,
marks the row and report `complete=false`, and returns exit code 2. Other valid
scene rows remain available in that report. A descriptor version is `null` when
native parsing fails before a valid scene is available. Missing/invalid roots or
output failures also return 2. An empty scene list is valid for a scene-free root.
The output schema is `Schemas/oxygen.cooked-scenes.schema.json`, installed with
Inspector tooling. This command contains no qualification hooks or payloads.

### Dependency metadata

```powershell
Oxygen.Cooker.Inspector.exe dependencies <cooked_root> --output <report.json>
```

Writes `oxygen.cooked-dependencies.v1` metadata using the runtime descriptor
decoders. Geometry and scene asset-key references are collected without loading
vertex buffers, textures, or a rendering engine. Material resource indices remain
local to their container. Each asset reports whether dependency inspection is
complete; unsupported types, malformed descriptors and scene script sidecars
retain a diagnostic instead of claiming an empty dependency set.

The report schema is installed as `oxygen.cooked-dependencies.schema.json`.

### Virtual asset identities

```powershell
Oxygen.Cooker.Inspector.exe asset-keys --input <request.json> --output <map.json>
```

The request uses `oxygen.asset-key-request.v1` with a `virtual_paths` array of
canonical native paths. The `oxygen.asset-key-map.v1` response supplies each
path and its engine-generated key using `AssetKey::FromVirtualPath`. This is a
batched metadata operation; source files and cooked containers are not required.
Invalid requests leave an existing output report untouched. Request and response
schemas are installed alongside the other cooker schemas.

## Exit codes

- `0`: success
- `1`: CLI usage / unknown command
- `2`: validation or runtime error while loading/inspecting
- `3`: unexpected top-level failure

## Implementation notes

This tool intentionally depends only on exported module APIs:

- `oxygen::content::AssetLoader` for validation
- `oxygen::content::lc::Inspection` for inspection output

## License

Distributed under the 3-Clause BSD License (see repository LICENSE).
