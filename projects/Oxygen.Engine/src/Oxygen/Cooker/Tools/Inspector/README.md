# Oxygen.Cooker.Inspector

A developer diagnostics tool to validate and inspect **loose cooked** content
roots.

Use [validation](#validate-a-cooked-root) for a pass/fail check,
[inventory](#export-the-integrity-inventory) for machine-readable file results,
or [index inspection](#dump-the-index) for asset and resource metadata.

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

### Export the integrity inventory

```powershell
Oxygen.Cooker.Inspector.exe inventory <cooked_root> --output <report.json>
```

The report includes the index identity, complete member sizes/digests and native
file roles, keyed asset identities, resource descriptors and integrity issues.
Texture resource rows carry a validated table index when the root is healthy;
they remain separate from keyed assets. Native validation checks OTEX encoding,
index bounds and agreement with the texture table. Place reports outside the root.
An empty `issues` array means full file verification succeeded. Exit code zero
means the report was produced; automation must also check `issues` before accepting
the content. The installed `oxygen.cooked-inventory.schema.json` defines the report.

Use this command for publication and explicit validation. Normal runtime mounting
checks metadata without scanning every payload; it can opt into full hashing.

### Dump the index

```powershell
Oxygen.Cooker.Inspector.exe index <cooked_root> [--assets true] [--files true] [--digests true]
```

Notes:

- If neither `--assets` nor `--files` is specified, both sections are printed.
- `--digests` includes the mandatory SHA-256 values recorded in the index.
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

`validate` checks scene, material and geometry descriptors through their native
parse-only loaders in addition to index, type and file-size checks. Retired
versions, malformed records, invalid material emission or slot identities fail
validation; content must be recooked.
Scene material overrides are also checked against geometry inventories present
in the same root: unknown slots or stale layout revisions require repair.
References to geometry in other mounted roots require validation with those
roots available; this command does not invent missing inventories.

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

### Geometry material-slot inventories

```powershell
Oxygen.Cooker.Inspector.exe geometries <cooked_root> --output <geometries.json>
```

Writes schema version 1 geometry inventories using `GeometryAsset::MaterialSlots()`.
Each geometry reports its asset key, SHA-256 layout revision and ordered slots.
Each slot retains its opaque ID, display label and exact LOD/submesh/default
material bindings. Equal labels or material keys never merge slots. Nil default
material keys remain the omitted-reference sentinel.

The native loader supplies these records without loading vertex/index buffers or
starting the renderer. An invalid geometry returns exit code 2 before writing a
report; successful reports contain every geometry in the root. Consumers must
check the exit code before using output. The installed schema is
`Schemas/oxygen.cooked-geometries.schema.json`.
Use `--virtual-path /Content/Geometry/example.ogeo` to inspect one selected
geometry without parsing other descriptors. A missing path yields an empty report.

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
