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

## Exit codes

The [toolkit's codes](../Common/README.md#exit-codes). A cooked root that does
not exist is refused with 1, as is an unusable `--output` or an invalid request.
A root that exists and does not read, or a report row that could not be read,
is a failed run: 2. A report with rows is written first.

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
file roles (resource data files list an all-zero `sha256`: the index records no
whole-file digest for them), keyed asset identities, resource descriptors and integrity issues.
Texture resource rows carry a validated table index when the root is healthy;
they remain separate from keyed assets. Native validation checks OTEX encoding,
index bounds and agreement with the texture table. Place reports outside the
root. An empty `issues` array means full file verification succeeded. Exit code
zero means the report was produced; automation must also check `issues` before
accepting the content. The installed `oxygen.cooked-inventory.schema.json`
defines the report.

Full verification checks each resource of a data file against the SHA-256 of its
table row. An issue about a resource carries the reason `malformed_table`,
`resource_out_of_bounds` or `resource_digest_mismatch`, and the optional
`resource_row` it concerns in the data file its `relative_path` names.

A mount accepts what a writer has not committed, but a root to publish holds
none of it, so it is an issue here: `uncommitted` for a writer's file no commit
lists, `uncommitted_tail` for bytes past a data file's committed length.

Use this command for publication and explicit validation. Normal runtime
mounting checks metadata without scanning every payload; it can opt into full
hashing.

### Dump the index

```powershell
Oxygen.Cooker.Inspector.exe index <cooked_root> [--assets true] [--files true] [--digests true]
```

Notes:

- If neither `--assets` nor `--files` is specified, both sections are printed.
- `--digests` includes the SHA-256 values recorded in the index (all zero for
  resource data files).
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
versions, malformed records, invalid material emission or surface identities fail
validation; content must be recooked. Scene material overrides are also checked
against geometry inventories present in the same root: unknown surfaces or stale
layout revisions require repair. References to geometry in other mounted roots
require validation with those roots available; this command does not invent
missing inventories.

```powershell
Oxygen.Cooker.Inspector.exe scenes <cooked_root> --output <scenes.json> [--virtual-path <scene>]
```

Writes `oxygen.cooked-scenes`, `schema_version` 3: each scene of the root composed, or only the
scene `--virtual-path` names (an unknown path reports no scene). A scene follows
its instances through the root's other scenes; a scene without instances
composes to itself, so every row has one shape:

- `occurrences`: the scene, then one per instance, depth first, each with its
  parent occurrence and the instance root it hangs under.
- `nodes`: every node of the composed tree, owned and instanced, with its
  defining scene and index there, its node ID, the composed scene's node it
  sits under (`root`, itself for an owned node) and the node path below it,
  its parent and, on an instance root, the instance's `source`.
- Per node, each component with every changeable property's value in the scene
  schema's units (degrees; Euler angles from the cooked quaternion; an enum is
  its member's wire value, so a flag source is Inherit, Shown/On or
  Hidden/Off), the geometry with the values it replaces, and the script slots
  with their parameters. Each value names the `supplier` occurrence: the scene
  whose change wins, or the defining scene.
- `unresolved`: the changes whose node path names no node, addressed as their
  scene made them, and `issues`: missing or unreadable instanced scenes,
  cycles and changes whose value fails validation.

Values are cooked records with every change applied, not runtime-effective
visibility, lighting eligibility or rendered results. Composition is the
engine's own (`Data/SceneComposition.h`), the one the runtime instantiates
with.

Every scene is parsed with the native loader, without resource loading or
renderer startup. A malformed or incomplete scene adds a diagnostic scoped to
its key/path, marks the row and report `complete=false`, and returns exit
code 2. Other valid scene rows remain available in that report. A descriptor
version is `null` when native parsing fails before a valid scene is available.
An empty scene list is valid for a scene-free root. The output schema is
`Schemas/oxygen.cooked-scenes.schema.json`, installed with Inspector tooling.
This command contains no qualification hooks or payloads.

### Geometry surface inventories

```powershell
Oxygen.Cooker.Inspector.exe geometries <cooked_root> --output <geometries.json>
```

Writes schema version 2 geometry inventories using
`GeometryAsset::Surfaces()`. Each geometry reports its asset key, virtual path,
`complete`, SHA-256 layout revision, default LOD policy (`lod_policy`, in the shared
`oxygen.lod-policy.schema.json` spelling) and ordered surfaces. Each surface
retains its opaque ID, display label, asset defaults (`visible`,
`casts_shadows`) and exact LOD/submesh/default material bindings, each with
the triangles its submesh draws (`triangle_count`, `SubMesh::TriangleCount()`).
Equal labels or material keys never merge surfaces. Nil default material keys
remain the omitted-reference sentinel.

The native loader supplies these records without loading vertex/index buffers or
starting the renderer. An invalid geometry adds a `diagnostic` to its row, marks
the row and report `complete=false`, and returns exit code 2; the other rows
remain available. The installed schema is
`Schemas/oxygen.cooked-geometries.schema.json`. Use
`--virtual-path /Content/Geometry/example.ogeo` to inspect one selected geometry
without parsing other descriptors. A missing path yields an empty report.

### Script metadata

```powershell
Oxygen.Cooker.Inspector.exe scripts <cooked_root> --output <scripts.json>
```

Writes schema version 1 script declarations using `ScriptAsset::Parameters()`.
Each script reports its virtual path, asset key, `complete` and the parameters
it declares,
in declaration order: name, type (`bool`, `int32`, `float`, `string`,
`vec2`-`vec4`, `asset` or `node`), the default for value types and the asset
kind for asset parameters. Authoring hints (range, unit, description, options,
color) are not cooked, so they are not reported.

The native loader reads only the descriptor; bytecode is not loaded. An invalid
script adds a `diagnostic` to its row, marks the row and report `complete=false`,
and returns exit code 2; the other rows remain available. The installed schema is
`Schemas/oxygen.cooked-scripts.schema.json`. Use
`--virtual-path /Content/Scripts/example.oscript` to inspect one script.

### Dependency metadata

```powershell
Oxygen.Cooker.Inspector.exe dependencies <cooked_root> --output <report.json>
```

Writes `schema_version` 4 metadata from the reference record the container
stores for each asset: the loose index or the pak catalog. No descriptor is
decoded, and nothing is loaded. Local resource indices remain local to their
container; a texture another container holds appears as an `external` binding
with its texture key among the key references. A root whose reference record
cannot be read is corrupt, so the command fails with exit code 2 and writes no
report; there are no partial rows.

The report schema is installed as `oxygen.cooked-dependencies.schema.json`.

### Virtual asset identities

```powershell
Oxygen.Cooker.Inspector.exe asset-keys --input <request.json> --output <map.json>
```

The request uses `oxygen.asset-key-request`, `schema_version` 2, with a `virtual_paths` array of
canonical native paths. The `oxygen.asset-key-map` response supplies each
path and its engine-generated key using `AssetKey::FromVirtualPath`. This is a
batched metadata operation; source files and cooked containers are not required.
Invalid requests leave an existing output report untouched. Request and response
schemas are installed alongside the other cooker schemas.

### Versions

`versions` prints the version of every cooked format and asset the tool reads,
from `PakFormatVersions.inc`; `--json` prints them as one object. The tool
reads only these versions.

## Exit codes

- `0`: success
- `1`: CLI usage error, including an unknown command or option
- `2`: validation or runtime error while loading/inspecting, or an unexpected
  failure

An `--output` path is absolute or relative to the working directory. A
destination that is the command's input (`report.destination_is_input`) or lies
in its cooked source (`report.destination_in_cooked_root`) exits `1` and writes
nothing. Reports are published atomically; one that cannot be written is
`report.write_failed` and exits `2`.

## Implementation notes

This tool intentionally depends only on exported module APIs:

- `oxygen::content::AssetLoader` for validation
- `oxygen::content::cooked::CookedContent` for inspection output

## License

Distributed under the 3-Clause BSD License (see repository LICENSE).
