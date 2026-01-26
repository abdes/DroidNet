# ImportTool Design Specification

## 1. Executive Summary

ImportTool is the authoritative command‑line interface for ingesting external assets (textures, FBX scenes, glTF/GLB models) into Oxygen's loose cooked content format. It is designed for production pipeline integration with concurrent batch execution, detailed progress reporting, and deterministic output for CI/CD workflows.

**Program Name:** `Oxygen.Content.ImportTool`
**Architecture:** Multi‑command Clap CLI with async execution runtime
**Current Version:** 0.1 (texture and batch commands operational)

---

## 2. Module Dependencies and Grounding

This specification is grounded in the following modules and headers from `Oxygen::Content` and `Oxygen::Data`. These modules are the authoritative source of types, behaviors, and data contracts referenced by requirements below.

### 2.1 Import Pipeline Core

- **`ImportRequest.h`**: Format‑agnostic job description containing source path, output destination, import format enum (`kTexture`, `kFbx`, `kGltf`), and per‑format option union.
- **`ImportOptions.h`**: Comprehensive option structure including:
  - `ImportContentFlags` (textures, materials, geometry, scene emission flags)
  - `CoordinateConversionPolicy` (unit normalization, transform baking)
  - `GeometryAttributePolicy` (normals, tangents generation/preservation)
  - `NodePruningPolicy` (scene graph optimization)
  - `TextureImportTuning` (per‑texture overrides)
- **`AsyncImportService.h`**: Submission, execution, progress tracking, and completion notification interface. Supports concurrent job execution with configurable in‑flight limits.

### 2.2 Texture Import Path

- **`TextureImporter.h`**: Texture‑specific import implementation with mip generation, format conversion, and cubemap processing.
- **`TextureImportTypes.h`**: Type definitions for:
  - `TextureIntent` (albedo, normal, roughness, metallic, ao, emissive, opacity, orm, hdr_env, hdr_probe, data, height)
  - `ColorSpace` (sRGB, linear)
  - `OutputFormat` (RGBA8, RGBA8_sRGB, BC7, BC7_sRGB, RGBA16F, RGBA32F)
  - `MipPolicy` (none, full, max), `MipFilter` (box, kaiser, lanczos)
  - `BC7Quality` (none, fast, default, high)
  - `PackingPolicy` (d3d12, tight)
  - `CubemapLayout` (auto, hstrip, vstrip, hcross, vcross)
- **`TextureImportPresets.h`**: Named texture configuration profiles (albedo_srgb, normal_linear, orm_linear, etc.)

### 2.3 Manifest and Batch Execution

- **`ImportManifest.h`**: Manifest data structure supporting versioned schema, job arrays, defaults, and per‑job overrides.
- **`ImportManifest_schema.h`**: JSON Schema (Draft‑7) definition and validator integration.
- **`ImportManifest.h`**: Manifest deserialization with schema validation and error reporting via `ImportManifest::Load()`.
- **`ImportDiagnostics.h`**: Canonical diagnostics structure for validation and import failures.

### 2.4 Reporting and Asset Metadata (Oxygen::Content and Oxygen::Data)

- **`ImportReport.h`**: Report schema and writer helpers for ImportTool JSON reports.
- **`LooseCookedLayout.h`**: Authoritative layout for cooked output paths and tables.
- **`TextureResource.h`**, **`BufferResource.h`**: Resource metadata for report entries.
- **`GeometryAsset.h`**, **`MaterialAsset.h`**, **`SceneAsset.h`**: Asset metadata schema referenced by report sections.
- **`AssetKey.h`**, **`AssetType.h`**, **`LooseCookedIndexFormat.h`**: Asset identity, type system, and index format used in dependency lists.

### 2.5 Tool Implementation (Current)

- **`TextureCommand.cpp`**: Single texture import command implementation with Clap option binding to `TextureImportSettings` and `ImportRequest`.
- **`BatchCommand.cpp`**: Manifest‑driven batch runner with TUI/non‑TUI execution modes, progress aggregation, and report generation.
- **`ImportRunner.cpp`**: Job execution wrapper integrating `AsyncImportService` with progress callbacks and telemetry collection.

---

## 3. Requirements Overview

### 3.1 Functional Requirements

1. **CLI Surface**: Support single‑asset and batch import workflows with consistent option semantics across all job types.
2. **Job Types**: Implement texture, FBX, and glTF/GLB import with format‑specific configuration.
3. **Manifest Format**: Provide JSON‑based batch job specification with versioning, defaults, overrides, and dependency ordering.
4. **Execution Model**: Concurrent job execution with configurable parallelism, failure policies, and resource throttling.
5. **Progress Reporting**: Real‑time progress updates via optional TUI or structured text output.
6. **Result Reporting**: Machine‑readable JSON reports with asset metadata, diagnostics, telemetry, and verification data.

### 3.2 Non‑Functional Requirements

1. **Performance**: Support 100+ concurrent jobs with minimal scheduler overhead.
2. **Robustness**: Validate all inputs early; fail fast with actionable error messages.
3. **Determinism**: Produce identical output for identical input (modulo timestamps).
4. **Automation**: Exit codes, structured logs, and JSON output suitable for CI/CD integration.
5. **Usability**: Built‑in help with examples; consistent option naming; clear error messages.

---

## 4. CLI Surface Specification

### 4.0 Command List and Conventions

**REQ‑CLI‑000**: The CLI SHALL expose the following top‑level commands:

- `texture`: import a single texture file.
- `fbx`: import a single FBX scene file.
- `gltf`: import a single glTF/GLB file.
- `batch`: execute a manifest of multiple jobs.
- `help`, `version`: built‑in Clap commands.

**REQ‑CLI‑000.1**: All options and subcommands SHALL use kebab‑case naming and follow Clap standard help rendering.

**REQ‑CLI‑000.2**: Boolean options SHALL use positive form where practical and support `--no-<option>` negations where disabling is meaningful.

**REQ‑CLI‑000.3**: Short options SHALL be provided for the most frequently used flags and paths only (e.g., `-o` for output, `-q` for quiet), to avoid collisions and preserve clarity.

### 4.1 Global Options

**REQ‑CLI‑001**: The tool SHALL accept the following global options applicable to all commands:

| Option | Type | Default | Description | Maps To |
| --- | --- | --- | --- | --- |
| `--quiet` / `-q` | flag | false | Suppress non‑error output | Console output control |
| `--log-level` | enum | `info` | Logging level: `trace`, `debug`, `info`, `warn`, `error` | Logger configuration |
| `--log-format` | enum | `text` | Log output format: `text`, `json` | Logger formatter |
| `--diagnostics-file` | path | none | Write structured diagnostics to file | Diagnostics output |
| `--cooked-root` | path | none | Default output directory for all jobs | `ImportRequest::output_root` |
| `--fail-fast` | flag | false | Stop on first job failure | Batch execution policy |
| `--no-color` | flag | false | Disable ANSI color codes | Terminal output formatting |
| `--theme` | enum | `dark` | Color theme: `plain`, `dark`, `light` | Help/usage theme selection |

**REQ‑CLI‑002**: Global `--cooked-root` SHALL be overridden by per‑command or per‑job `--output` options.

**REQ‑CLI‑003**: All global options SHALL appear before the command name in the invocation syntax.

**REQ‑CLI‑004**: Option precedence SHALL follow this order (highest to lowest):

1. Explicit command‑line options
2. Per‑job overrides in manifest (batch only)
3. Manifest defaults (batch only)
4. Command‑specific defaults
5. Global defaults

**REQ‑CLI‑005**: Global options SHALL be inherited by all commands unless explicitly overridden by command‑specific options (e.g., `--output`) or per‑job overrides.

**Acceptance Criteria:**

- AC‑CLI‑001: `--help` displays global options before command list.
- AC‑CLI‑002: Global options are parsed and applied before command execution.
- AC‑CLI‑003: Invalid global option values produce actionable error messages.
- AC‑CLI‑004: Precedence order yields deterministic option resolution in mixed CLI + manifest inputs.

---

### 4.2 Command: `texture`

**REQ‑TEX‑001**: Import a single texture image file into loose cooked format.

**REQ‑TEX‑002**: Positional argument `SOURCE` (required) SHALL specify the input image file path.

**REQ‑TEX‑002.1**: The command SHALL set `ImportRequest::import_format` to `kTexture` and populate the texture option union as defined in `ImportRequest.h`.

**REQ‑TEX‑003**: Options SHALL map to `TextureImportSettings` and `ImportOptions::texture_tuning` as follows:

#### 4.2.1 Job Identity and Output

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--output` / `-o` | path | yes | none | `ImportRequest::output_root` |
| `--name` | string | no | derived from source filename | `ImportRequest::job_name` |
| `--report` | path | no | none | Report output path |

**REQ‑TEX‑004**: If `--output` is not provided and no global `--cooked-root` is set, the command SHALL fail with error code 2.

#### 4.2.2 Texture Intent and Color Space

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--intent` | enum | no | `data` | `TextureImportSettings::intent` |
| `--color-space` | enum | no | derived from intent | `TextureImportSettings::color_space` |

**REQ‑TEX‑005**: Valid `--intent` values:

- `albedo`, `normal`, `roughness`, `metallic`, `ao`, `emissive`, `opacity`, `orm`, `hdr-env`, `hdr-probe`, `data`, `height`

**REQ‑TEX‑006**: Valid `--color-space` values: `srgb`, `linear`

**REQ‑TEX‑007**: Default color space SHALL be `srgb` for `albedo`, `emissive`; `linear` for all others.

#### 4.2.3 Output Format Control

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--output-format` | enum | no | derived from intent | `TextureImportSettings::output_format` |
| `--data-format` | enum | no | none | Format override for `data` intent |
| `--packing-policy` | enum | no | `d3d12` | `TextureImportSettings::packing_policy` |

**REQ‑TEX‑008**: Valid `--output-format` values:

- `rgba8`, `rgba8-srgb`, `bc7`, `bc7-srgb`, `rgba16f`, `rgba32f`

**REQ‑TEX‑009**: Default format mapping:

- `albedo`, `emissive` → `bc7-srgb`
- `normal`, `orm` → `bc7`
- `roughness`, `metallic`, `ao`, `opacity` → `bc7`
- `hdr-env`, `hdr-probe` → `rgba16f`
- `height` → `rgba16f`
- `data` → `rgba8`

**REQ‑TEX‑010**: `--data-format` SHALL override format only when `--intent=data`.

#### 4.2.4 Mip Generation

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--mip-policy` | enum | no | `full` | `TextureImportSettings::mip_policy` |
| `--max-mips` | uint | no | unlimited | `TextureImportSettings::max_mips` |
| `--mip-filter` | enum | no | `kaiser` | `TextureImportSettings::mip_filter` |
| `--bc7-quality` | enum | no | `default` | `TextureImportSettings::bc7_quality` |

**REQ‑TEX‑011**: Valid `--mip-policy` values: `none`, `full`, `max`

**REQ‑TEX‑012**: `--max-mips` SHALL only apply when `--mip-policy=max`.

**REQ‑TEX‑013**: Valid `--mip-filter` values: `box`, `kaiser`, `lanczos`

**REQ‑TEX‑014**: Valid `--bc7-quality` values: `none`, `fast`, `default`, `high`

**REQ‑TEX‑015**: `--bc7-quality` SHALL only apply when output format is `bc7` or `bc7-srgb`.

#### 4.2.5 Cubemap Processing

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--cubemap` | flag | no | false | `TextureImportSettings::is_cubemap` |
| `--equirect-to-cube` | flag | no | false | `TextureImportSettings::convert_equirect_to_cube` |
| `--cube-face-size` | uint | no | auto | `TextureImportSettings::cube_face_size` |
| `--cube-layout` | enum | no | `auto` | `TextureImportSettings::cube_layout` |

**REQ‑TEX‑016**: Valid `--cube-layout` values: `auto`, `hstrip`, `vstrip`, `hcross`, `vcross`

**REQ‑TEX‑017**: `--equirect-to-cube` SHALL set `--cubemap=true` implicitly.

**REQ‑TEX‑018**: If `--cubemap` is set and layout is `auto`, the tool SHALL detect layout from source dimensions.

#### 4.2.6 Decode Modifiers

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--flip-y` | flag | no | false | `TextureImportSettings::flip_y` |
| `--force-rgba` | flag | no | false | `TextureImportSettings::force_rgba` |

#### 4.2.7 Presets

**REQ‑TEX‑019**: Support `--preset` option to load named texture configuration profiles from `TextureImportPresets`.

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--preset` | string | no | none | `TextureImportPresets::GetPreset()` |

**REQ‑TEX‑020**: When `--preset` is specified, preset values SHALL be applied first, then overridden by explicit command‑line options.

**REQ‑TEX‑021**: Built‑in preset names SHALL include:

- `albedo-srgb`, `albedo-linear`
- `normal-linear`, `normal-bc7`
- `orm-bc7`, `orm-tight`
- `hdr-env-16f`, `hdr-probe-16f`
- `data-rgba8`, `data-rgba16f`

#### 4.2.8 Example Invocations

```bash
# Basic albedo texture with defaults
Oxygen.Content.ImportTool texture input.png --output ./Cooked --intent albedo

# Normal map with custom mip settings
Oxygen.Content.ImportTool texture normal.png -o ./Cooked \
  --intent normal --mip-filter lanczos --bc7-quality high

# HDR environment map with cubemap conversion
Oxygen.Content.ImportTool texture env.exr -o ./Cooked \
  --intent hdr-env --equirect-to-cube --cube-face-size 1024

# Using preset with override
Oxygen.Content.ImportTool texture albedo.png -o ./Cooked \
  --preset albedo-srgb --bc7-quality high
```

**Acceptance Criteria:**

- AC‑TEX‑001: All texture intents produce valid cooked output.
- AC‑TEX‑002: Invalid intent/format combinations are rejected with clear error messages.
- AC‑TEX‑003: Preset application followed by override produces expected results.
- AC‑TEX‑004: Cubemap layout detection succeeds for standard layouts.
- AC‑TEX‑005: JSON report includes all telemetry and asset metadata.

---

### 4.3 Command: `batch`

**REQ‑BATCH‑001**: Execute multiple import jobs from a JSON manifest with concurrent execution.

**REQ‑BATCH‑002**: Positional argument `MANIFEST` (required) SHALL specify the manifest file path.

#### 4.3.1 Job Selection and Execution

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--root` | path | no | manifest directory | Base path for relative source paths |
| `--dry-run` | flag | no | false | Validation‑only mode |
| `--fail-fast` | flag | no | false | Stop on first job failure |

**REQ‑BATCH‑003**: `--dry-run` SHALL validate manifest, resolve paths, and print job summary without executing imports.

**REQ‑BATCH‑004**: `--fail-fast` SHALL terminate all in‑flight jobs on first failure.

#### 4.3.2 Output and Reporting

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--report` | path | no | none | Batch report output path |
| `--quiet` / `-q` | flag | no | false | Suppress non‑error output |

**REQ‑BATCH‑005**: If `--report` is a relative path, it SHALL be resolved
relative to the cooked root (first completed job report, otherwise the first
job's cooked root). Relative report paths require a cooked root; otherwise
the command SHALL fail with an error.

#### 4.3.3 TUI Control

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--no-tui` | flag | no | false | Disable curses UI |

**REQ‑BATCH‑006**: TUI SHALL be automatically disabled if:

- `--no-tui` is set
- `stdout` is not a TTY
- `TERM` environment variable is unset or `dumb`

**REQ‑BATCH‑007**: When TUI is disabled, progress SHALL be printed as structured text with timestamps.

#### 4.3.4 Example Invocations

```bash
# Basic batch execution with TUI
Oxygen.Content.ImportTool batch import-manifest.json --output ./Cooked

# Dry run for validation
Oxygen.Content.ImportTool batch import-manifest.json --dry-run

# CI mode: no TUI, JSON logs, fail fast
Oxygen.Content.ImportTool batch import-manifest.json \
  --no-tui --fail-fast --log-format json --report report.json
```

**Acceptance Criteria:**

- AC‑BATCH‑001: Valid manifest with 50+ jobs executes to completion.
- AC‑BATCH‑002: Dry run detects all validation errors without execution.
- AC‑BATCH‑003: TUI displays real‑time progress for all in‑flight jobs.
- AC‑BATCH‑004: Non‑TUI mode produces parseable text output with timestamps.
- AC‑BATCH‑005: Fail‑fast mode stops execution on first error within 100ms.

---

### 4.4 Command: `fbx`

**REQ‑FBX‑001**: Import FBX scene files into loose cooked content.

**REQ‑FBX‑002**: Positional argument `SOURCE` (required) SHALL specify the FBX file path.

**REQ‑FBX‑002.1**: The command SHALL set `ImportRequest::import_format` to `kFbx` and populate FBX‑relevant fields in `ImportOptions`.

#### 4.4.1 Job Identity and Output

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--output` / `-o` | path | yes | none | `ImportRequest::output_root` |
| `--name` | string | no | derived from source | `ImportRequest::job_name` |
| `--report` | path | no | none | Report output path |

#### 4.4.2 Content Emission Flags

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--no-import-textures` | flag | no | false | `ImportOptions::import_content` (disable `kTextures`) |
| `--no-import-materials` | flag | no | false | `ImportOptions::import_content` (disable `kMaterials`) |
| `--no-import-geometry` | flag | no | false | `ImportOptions::import_content` (disable `kGeometry`) |
| `--no-import-scene` | flag | no | false | `ImportOptions::import_content` (disable `kScene`) |

**REQ‑FBX‑003**: All content flags default to enabled; use `--no-import-<type>` to disable.

#### 4.4.3 Coordinate Conversion and Units

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--unit-policy` | enum | no | `normalize` | `ImportOptions::coordinate.unit_normalization` |
| `--unit-scale` | float | no | 1.0 | `ImportOptions::coordinate.unit_scale` |
| `--no-bake-transforms` | flag | no | false | `ImportOptions::coordinate.bake_transforms_into_meshes` (disable) |

**REQ‑FBX‑004**: Valid `--unit-policy` values: `normalize`, `preserve`, `custom`

**REQ‑FBX‑005**: `--unit-scale` SHALL only apply when `--unit-policy=custom`.

**REQ‑FBX‑006**: `--no-bake-transforms` SHALL disable transform baking (default is enabled by importer).

#### 4.4.4 Geometry Processing

| Option | Type | Required | Default | Maps To |
| --- | --- | --- | --- | --- |
| `--normals` | enum | no | `generate` | `ImportOptions::normal_policy` |
| `--tangents` | enum | no | `generate` | `ImportOptions::tangent_policy` |
| `--prune-nodes` | enum | no | `keep` | `ImportOptions::node_pruning` |

**REQ‑FBX‑007**: Valid `--normals` values: `none`, `preserve`, `generate`, `recalculate`

**REQ‑FBX‑008**: Valid `--tangents` values: `none`, `preserve`, `generate`, `recalculate`

**REQ‑FBX‑009**: Valid `--prune-nodes` values: `keep`, `drop-empty`

#### 4.4.5 Example Invocations

```bash
# Full FBX import with defaults
Oxygen.Content.ImportTool fbx scene.fbx --output ./Cooked

# Geometry only, normalized units
Oxygen.Content.ImportTool fbx mesh.fbx -o ./Cooked \
  --no-import-textures --no-import-materials --unit-policy normalize

# Custom unit scale with transform baking disabled
Oxygen.Content.ImportTool fbx scene.fbx -o ./Cooked \
  --unit-policy custom --unit-scale 0.01 --no-bake-transforms
```

**Acceptance Criteria:**

- AC‑FBX‑001: FBX files with embedded textures (RESOURCES) are imported correctly.
- AC‑FBX‑002: Unit normalization produces consistent world‑space scales.
- AC‑FBX‑003: Transform baking produces identity node transforms.
- AC‑FBX‑004: Node pruning removes expected empty nodes.
- AC‑FBX‑005: Generated normals and tangents match reference implementation.

---

### 4.5 Command: `gltf`

**REQ‑GLTF‑001**: Import glTF 2.0 or GLB files into loose cooked content.

**REQ‑GLTF‑002**: Positional argument `SOURCE` (required) SHALL specify the `.gltf` or `.glb` file path.

**REQ‑GLTF‑002.1**: The command SHALL set `ImportRequest::import_format` to `kGltf` and populate glTF‑relevant fields in `ImportOptions`.

**REQ‑GLTF‑003**: Options SHALL mirror FBX command where applicable, with glTF‑specific defaults.

#### 4.5.1 Job Identity and Output

(Same as FBX: `--output`, `--name`, `--report`)

#### 4.5.2 Content Emission Flags

(Same as FBX: `--no-import-textures`, `--no-import-materials`, `--no-import-geometry`, `--no-import-scene`)

#### 4.5.3 Coordinate Conversion and Units

**REQ‑GLTF‑004**: glTF files SHALL assume 1 unit = 1 meter by default.

**REQ‑GLTF‑005**: `--unit-policy` default SHALL be `normalize` (importer default).

**REQ‑GLTF‑006**: glTF Y‑up right‑handed coordinate system SHALL be converted to Oxygen's coordinate system automatically.

#### 4.5.4 Geometry Processing

(Same as FBX: `--normals`, `--tangents`, `--prune-nodes`)

**REQ‑GLTF‑007**: glTF tangents SHALL use the importer’s tangent policy.

#### 4.5.5 Material Conventions

**REQ‑GLTF‑008**: glTF PBR materials SHALL be converted to Oxygen material format with ORM packing (Occlusion, Roughness, Metallic).

**REQ‑GLTF‑009**: Texture packing policy SHALL default to `tight` for ORM textures.

#### 4.5.6 Example Invocations

```bash
# Full glTF import
Oxygen.Content.ImportTool gltf model.gltf --output ./Cooked

# GLB with custom processing
Oxygen.Content.ImportTool gltf model.glb -o ./Cooked \
  --tangents recalculate --prune-nodes drop-empty
```

**Acceptance Criteria:**

- AC‑GLTF‑001: glTF and GLB files produce identical output.
- AC‑GLTF‑002: PBR materials are converted with correct ORM packing.
- AC‑GLTF‑003: Coordinate system conversion preserves visual appearance.
- AC‑GLTF‑004: Embedded GLB textures are extracted and imported.

---

### 4.6 Built‑In Commands

**REQ‑CLI‑100**: The tool SHALL provide built‑in `help` and `version` commands via Clap framework.

**REQ‑CLI‑101**: `help` output SHALL include:

- Global options with descriptions
- Command list with one‑line summaries
- Per‑command detailed help with option tables
- Example invocations for each command

**REQ‑CLI‑102**: `version` output SHALL include:

- Tool version from VERSION file
- Rust toolchain version
- Clap framework version
- Copyright and license information

**Acceptance Criteria:**

- AC‑CLI‑100: `--help` and `help` produce identical output.
- AC‑CLI‑101: `help <command>` displays command‑specific help.
- AC‑CLI‑102: `--version` matches VERSION file content.

---

### 4.7 Exit Codes

**REQ‑CLI‑200**: The tool SHALL use the following exit code conventions:

| Code | Meaning | When Used |
| --- | --- | --- |
| 0 | Success | All jobs completed successfully |
| 1 | General error | Unspecified or internal error |
| 2 | Invalid arguments | Missing required options, invalid values |
| 3 | File not found | Source file does not exist |
| 4 | Validation error | Manifest schema validation failed |
| 5 | Job failure | One or more jobs failed (batch mode, no fail‑fast) |
| 6 | Execution timeout | Job execution exceeded timeout |
| 130 | User interrupt | SIGINT received (Ctrl+C) |

**REQ‑CLI‑201**: Exit codes SHALL be consistent across all commands.

**REQ‑CLI‑202**: Exit codes SHALL be documented in `--help` output.

**Acceptance Criteria:**

- AC‑CLI‑200: Each error condition produces the correct exit code.
- AC‑CLI‑201: CI integration tests verify exit code correctness.

---

## 5. Manifest Format Specification

**REQ‑MF‑000**: `ImportManifest_schema.h` is the authoritative schema definition. All manifest validation behavior SHALL be consistent with its Draft‑7 rules and error messages provided by `ImportManifest::Load()`.

### 5.1 JSON Schema Structure

**REQ‑MF‑001**: The manifest format SHALL use JSON with Draft‑7 schema validation via `ImportManifest`.

**REQ‑MF‑002**: The manifest top‑level structure SHALL include:

- `version` (integer, required): Version of the manifest schema (currently 1).
- `defaults` (object, optional): Global defaults for all jobs.
- `jobs` (array, required): Array of import job specifications.

**REQ‑MF‑003**: Schema version upgrades SHALL be validated against `ImportManifest_schema.h` with clear error messages for incompatible versions.

### 5.2 Global Defaults Block

**REQ‑MF‑010**: The `defaults` block SHALL support the following properties:

| Property | Type | Maps To | Description |
| --- | --- | --- | --- |
| `job_type` | string | Default job type | `texture`, `fbx`, or `gltf` |
| `cooked_root` | string | Output directory | Applied if per‑job `cooked_root` is absent |
| `job_name` | string | Job naming | Default job name when per‑job `job_name` is absent |
| `verbose` | boolean | Per‑job progress output | Default progress output for all jobs |
| `import_options` | object | Common options | Shared settings applied to all jobs before per‑job overrides |

**REQ‑MF‑011**: `import_options` in defaults SHALL support:

- `content_flags`: Bitmask (textures, materials, geometry, scene)
- `unit_policy`: `normalize`, `preserve`, `custom`
- `unit_scale`: floating‑point scale factor
- `bake_transforms`: boolean
- `normals_policy`: `none`, `preserve`, `generate`, `recalculate`
- `tangents_policy`: `none`, `preserve`, `generate`, `recalculate`
- `node_pruning`: `keep`, `drop-empty`

### 5.3 Job Specification

**REQ‑MF‑020**: Each job in the `jobs` array SHALL include:

| Field | Type | Required | Maps To |
| --- | --- | --- | --- |
| `id` | string | yes | Job identifier for diagnostics and reports |
| `job_type` | string | no | Overrides default `job_type` |
| `source` | string | yes | Primary source file path (absolute or relative to `--root`) |
| `sources` | array[string] | no | Additional source files (materials, atlases) |
| `cooked_root` | string | no | Output directory override |
| `job_name` | string | no | Human‑readable job name |
| `import_options` | object | no | Per‑job overrides to `defaults.import_options` |
| `texture` | object | no | Texture job specific settings (when `job_type=texture`) |
| `fbx` | object | no | FBX job specific settings (when `job_type=fbx`) |
| `gltf` | object | no | glTF job specific settings (when `job_type=gltf`) |
| `depends_on` | array[string] | no | List of job IDs that must complete before this job |
| `tags` | array[string] | no | Free‑form tags for filtering and reporting |

**REQ‑MF‑021**: Job IDs SHALL be unique within a manifest and suitable for correlation with job entries in reports.

**REQ‑MF‑022**: Relative source paths SHALL be resolved relative to the `--root` directory passed to `batch` command.

**REQ‑MF‑023**: Circular dependencies in `depends_on` SHALL be detected and reported with error code 4 (validation error).

### 5.4 Texture Job Configuration

**REQ‑MF‑030**: When `job_type=texture`, the `texture` block SHALL support:

| Field | Type | Maps To |
| --- | --- | --- |
| `intent` | string | `TextureImportSettings::intent` |
| `color_space` | string | `TextureImportSettings::color_space` |
| `output_format` | string | `TextureImportSettings::output_format` |
| `packing_policy` | string | `TextureImportSettings::packing_policy` |
| `mip_policy` | string | `TextureImportSettings::mip_policy` |
| `max_mips` | integer | `TextureImportSettings::max_mips` |
| `mip_filter` | string | `TextureImportSettings::mip_filter` |
| `bc7_quality` | string | `TextureImportSettings::bc7_quality` |
| `is_cubemap` | boolean | `TextureImportSettings::is_cubemap` |
| `convert_equirect_to_cube` | boolean | `TextureImportSettings::convert_equirect_to_cube` |
| `cube_face_size` | integer | `TextureImportSettings::cube_face_size` |
| `cube_layout` | string | `TextureImportSettings::cube_layout` |
| `flip_y` | boolean | `TextureImportSettings::flip_y` |
| `force_rgba` | boolean | `TextureImportSettings::force_rgba` |
| `preset` | string | Named preset from `TextureImportPresets` |

**REQ‑MF‑031**: Preset names in the `texture` block SHALL match those available in `TextureImportPresets::GetPreset()`.

### 5.5 FBX Job Configuration

**REQ‑MF‑040**: When `job_type=fbx`, the `fbx` block SHALL support:

| Field | Type | Maps To |
| --- | --- | --- |
| `content_flags` | object | `ImportOptions::import_content` |
| `unit_policy` | string | `ImportOptions::coordinate.unit_normalization` |
| `unit_scale` | number | `ImportOptions::coordinate.unit_scale` |
| `bake_transforms` | boolean | `ImportOptions::coordinate.bake_transforms_into_meshes` |
| `normals_policy` | string | `ImportOptions::normal_policy` |
| `tangents_policy` | string | `ImportOptions::tangent_policy` |
| `node_pruning` | string | `ImportOptions::node_pruning` |

**REQ‑MF‑041**: The `content_flags` object SHALL have boolean fields: `textures`, `materials`, `geometry`, `scene`.

### 5.6 glTF Job Configuration

**REQ‑MF‑050**: When `job_type=gltf`, the `gltf` block SHALL support the same fields as `fbx` with glTF‑specific semantics.

**REQ‑MF‑051**: glTF unit normalization default SHALL be `normalize` (inherits from defaults if not specified).

**REQ‑MF‑052**: ORM texture packing policy SHALL default to `tight` for glTF jobs.

### 5.7 Manifest Example

```json
{
  "schema_version": 1,
  "defaults": {
    "cooked_root": "./Cooked",
    "job_type": "texture",
    "verbose": false,
    "import_options": {
      "content_flags": { "textures": true, "materials": true, "geometry": true, "scene": true }
    }
  },
  "jobs": [
    {
      "id": "job-textures",
      "job_type": "texture",
      "source": "assets/character_albedo.png",
      "texture": {
        "intent": "albedo",
        "preset": "albedo-srgb",
        "bc7_quality": "high"
      }
    },
    {
      "id": "job-model",
      "job_type": "fbx",
      "source": "assets/character.fbx",
      "fbx": {
        "unit_policy": "normalize",
        "normals_policy": "generate",
        "tangents_policy": "recalculate"
      }
    }
  ]
}
```

**REQ‑MF‑060**: `depends_on` lists job IDs but only enforces ordering for ASSET\u2011to\u2011ASSET dependencies. Texture and buffer RESOURCE jobs always execute before ASSET jobs that reference them (implicitly ordered).

**Acceptance Criteria:**

- AC‑MF‑001: Schema validation rejects manifests with invalid `version`.
- AC‑MF‑002: Default values are applied correctly to all jobs.
- AC‑MF‑003: Per‑job overrides override defaults without side effects.
- AC‑MF‑004: Circular dependencies are detected before execution.
- AC‑MF‑005: Relative paths resolve correctly with `--root` override.

---

## 6. Execution Model Specification

### 6.1 Job Submission and Concurrency

**REQ‑EXE‑001**: Job execution SHALL use `AsyncImportService` from `Oxygen::Content` for concurrent execution.

**REQ‑EXE‑002**: The maximum number of concurrent jobs (in‑flight limit) SHALL
be controlled by the manifest `max_in_flight_jobs` field when provided, and
otherwise default to `std::thread::hardware_concurrency()`.

**REQ‑EXE‑003**: In batch mode, jobs with unsatisfied dependencies SHALL not be submitted to `AsyncImportService` until all dependencies complete successfully.

**REQ‑EXE‑004**: If a dependency job fails and `--fail-fast` is NOT set, dependent jobs SHALL be marked as skipped and reported with status `SKIPPED`.

**REQ‑EXE‑005**: If `--fail-fast` is set, all in‑flight jobs SHALL be cancelled within 100ms of first failure.

### 6.2 Progress Tracking and Callbacks

**REQ‑EXE‑010**: During execution, job progress SHALL be tracked with the following phases:

| Phase | Meaning | Telemetry |
| --- | --- | --- |
| `Pending` | Awaiting in‑flight slot | submission timestamp |
| `Loading` | Loading/parsing the source | start timestamp |
| `Planning` | Building the work plan | progress % if available |
| `Working` | Executing plan items | item start/finish + counts |
| `Finalizing` | Finalizing and writing outputs | end timestamp |
| `Complete` | Job finished (success/failure/cancel) | duration, status |

**REQ‑EXE‑011**: Progress callbacks SHALL report current phase, job ID, and elapsed time at least once per second for long‑running jobs (>2s).

### 6.3 Error Handling and Diagnostics

**REQ‑EXE‑020**: All errors SHALL be collected in a per‑job diagnostics list with:

- Error code (internal enum)
- Human‑readable message
- Stack trace (if available in debug builds)
- Affected file or resource

**REQ‑EXE‑021**: Early validation errors (file not found, invalid options) SHALL be reported before job submission.

**REQ‑EXE‑022**: Runtime errors during processing SHALL not crash the tool; instead, the job SHALL be marked as failed and remaining jobs continue (unless `--fail-fast`).

### 6.4 TUI and Non‑TUI Progress Output

**REQ‑EXE‑030**: When TUI is enabled:

- Display a real‑time grid showing job status (queued/processing/complete)
- Display current phase for in‑flight jobs
- Display error summary at bottom
- Support interactive pause/resume, job filtering (future enhancement)

**REQ‑EXE‑031**: When TUI is disabled (text mode):

- Print structured progress lines with timestamps in ISO 8601 format
- Format: `[YYYY-MM-DDTHH:MM:SSZ] [job-id] [PHASE] Message`
- Example: `[2026-01-21T15:30:45Z] [job-albedo] [Processing] Decoding PNG... (45%)`

**REQ‑EXE‑032**: All text output SHALL use UTF‑8 encoding and be suitable for piping to log files or parsers.

**Acceptance Criteria:**

- AC‑EXE‑001: Batch with 100 jobs respects the configured in‑flight limit
  throughout execution.
- AC‑EXE‑002: TUI updates at least once per second for active jobs.
- AC‑EXE‑003: Text mode output is machine‑parseable with grep/regex.
- AC‑EXE‑004: Dependency graph with 10+ jobs respects ordering constraints.
- AC‑EXE‑005: Fail‑fast cancellation completes within 100ms window.

---

## 7. Result Reporting Specification

### 7.0 Current Implementation Notes (Report v2)

- Reports are emitted only when `--report` is provided.
- Relative `--report` paths are resolved against the cooked root, then
  normalized.
- The report uses `report_version: "2"` with `session`, `summary`, and `jobs`.
- `jobs[*].status` values are `succeeded`, `failed`, `skipped`, and
  `not_submitted` (for jobs not submitted due to cancellation).
- `stats` is always present with `time_ms_total`, `time_ms_io`,
  `time_ms_decode`, `time_ms_load`, `time_ms_cook`, `time_ms_emit`, and
  `time_ms_finalize` (no nulls).
- `outputs` records are container‑relative and include emitted asset
  descriptors plus resource tables/data and `container.index.bin` when written.
- Missing outputs are treated as failures and emit diagnostics.
- Diagnostics are reported as emitted by importers (no extra synthesis beyond
  cancellation and missing output diagnostics).

**REQ‑REPORT‑000**: Report semantics and field naming SHALL align with `ImportReport.h` and with asset/resource metadata types in `Oxygen::Data` (see `GeometryAsset.h`, `MaterialAsset.h`, `SceneAsset.h`, `TextureResource.h`, `BufferResource.h`).

### 7.1 Report Structure and Schema

**REQ‑REPORT‑001**: JSON report output (emitted to `--report` path) SHALL conform to the following top‑level schema:

```json
{
  "schema_version": 1,
  "tool_version": "0.1.0",
  "timestamp": "2026-01-21T15:30:45.123456Z",
  "duration_ms": 1234,
  "command": "batch",
  "manifest_path": "/path/to/manifest.json",
  "summary": { },
  "jobs": [ ],
  "diagnostics": [ ]
}
```

**REQ‑REPORT‑002**: The `summary` object SHALL include:

| Field | Type | Meaning |
| --- | --- | --- |
| `total_jobs` | integer | Total jobs submitted |
| `successful_jobs` | integer | Jobs with status SUCCESS |
| `failed_jobs` | integer | Jobs with status FAILED |
| `skipped_jobs` | integer | Jobs skipped due to dependency failure |
| `total_bytes_input` | integer | Total input source file bytes |
| `total_bytes_emitted` | integer | Total bytes emitted to disk (descriptors + blobs) |
| `cooked_root` | string | Output root directory used |

**REQ‑REPORT‑003**: Terminology distinction (from `Oxygen::Content` architecture):

- **ASSETS**: Top‑level emitted items with descriptors (geometry, material, scene). Only assets can have inter‑asset dependencies.
- **RESOURCES**: Data referenced by assets (textures, buffers). Resources are NOT independently addressable in reports; they are listed as dependencies of their referencing assets.

### 7.2 Per‑Job Report Entry

**REQ‑REPORT‑010**: Each job entry in `jobs` array SHALL include:

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | Job ID from manifest |
| `type` | string | `texture`, `fbx`, or `gltf` |
| `source` | string | Normalized source path(s) |
| `status` | string | `SUCCESS`, `FAILED`, `SKIPPED` |
| `created_ms` | integer | Epoch timestamp when job created (ms) |
| `started_ms` | integer | Epoch timestamp when execution started (ms) |
| `completed_ms` | integer | Epoch timestamp when execution completed (ms) |
| `diagnostics` | array | Array of error/warning entries |
| `telemetry` | object | Timing breakdown and metrics |
| `assets` | array | Array of emitted asset entries (only ASSETS: geometry, material, scene) |
| `resource_count` | integer | Count of RESOURCES emitted (textures, buffers) |

**REQ‑REPORT‑011**: Diagnostics entries SHALL include:

| Field | Type | Meaning |
| --- | --- | --- |
| `level` | string | `ERROR` or `WARNING` |
| `code` | string | Internal error code (e.g., `ERR_DECODE_FAILED`) |
| `message` | string | Human‑readable message |
| `context` | object | Additional context (file path, line number, etc.) |

### 7.3 Telemetry Block

**REQ‑REPORT‑020**: The `telemetry` object per job SHALL include:

| Field | Type | Unit | Meaning |
| --- | --- | --- | --- |
| `io_ms` | integer | milliseconds | Time to read source file bytes |
| `decode_ms` | integer | milliseconds | Time to decode/transform source format |
| `load_ms` | integer | milliseconds | Time to load and prepare source data |
| `cook_ms` | integer | milliseconds | Time to process/cook content |
| `emit_ms` | integer | milliseconds | Time to write descriptor and resource files |
| `finalize_ms` | integer | milliseconds | Time to finalize and update index |
| `total_ms` | integer | milliseconds | Total job duration |
| `input_bytes` | integer | bytes | Total source file size |
| `emitted_bytes` | integer | bytes | Total bytes written to disk (descriptors + resource blobs) |

**REQ‑REPORT‑021**: Telemetry times are optional (may be omitted if not applicable to job type).

### 7.4 Asset Metadata (ASSETS Only)

**REQ‑REPORT‑030**: Each asset in the `assets` array represents an ASSET (geometry, material, or scene). Only ASSETS are reported; RESOURCES (textures, buffers) are listed as dependencies of their referencing assets and are NOT included as standalone report entries.

**REQ‑REPORT‑031**: Each asset entry SHALL include:

| Field | Type | Meaning |
| --- | --- | --- |
| `asset_type` | string | `geometry`, `material`, or `scene` |
| `asset_key` | string | Globally unique asset identifier (GUID or stable key) |
| `virtual_path` | string | Cooked asset virtual path (mounted path in container) |
| `descriptor_path` | string | Descriptor file path relative to cooked root |
| `descriptor_size_bytes` | integer | Descriptor file size |
| `metadata` | object | Format‑specific metadata (see 7.5) |
| `resource_dependencies` | array | List of RESOURCES (textures, buffers) this asset references with table indices |
| `asset_dependencies` | array | List of ASSET KEYs this asset depends on (e.g., materials used by geometry) |
| `hashes` | object | SHA‑256 hashes for verification (descriptor) |

**REQ‑REPORT‑032**: The `resource_dependencies` array lists references to textures and buffers using their cooked table indices (resource_index), with metadata (path, size, hash). Resources are identified by their unique table index, NOT by hash. Blob data is referenced by path only.

### 7.5 Asset Metadata by Type

#### Geometry Asset Metadata

**REQ‑REPORT‑040**:

```json
{
  "metadata": {
    "vertex_count": 450000,
    "index_count": 1350000,
    "lod_count": 3,
    "submesh_count": 5,
    "has_normals": true,
    "has_tangents": true,
    "vertex_format": "PNTU32"
  }
}
```

#### Material Asset Metadata

**REQ‑REPORT‑041**:

```json
{
  "metadata": {
    "material_type": "pbr_metallic_roughness",
    "texture_binding_count": 3,
    "packing_flags": ["ORM"]
  }
}
```

#### Scene Asset Metadata

**REQ‑REPORT‑042**:

```json
{
  "metadata": {
    "node_count": 125,
    "mesh_instances": 87,
    "material_instances": 12,
    "has_animations": false,
    "bounds": {
      "center": [-0.5, 1.0, 0.0],
      "extents": [10.0, 12.0, 15.0]
    }
  }
}
```

**REQ‑REPORT‑043**: Texture and buffer RESOURCES SHALL NOT be emitted as top‑level assets in the report. They are referenced via `resource_dependencies` arrays in assets that use them.

### 7.6 Report Example

```json
{
  "version": 1,
  "tool_version": "0.1.0",
  "timestamp": "2026-01-21T15:30:45.123456Z",
  "duration_ms": 5234,
  "command": "batch",
  "manifest_path": "./import-manifest.json",
  "summary": {
    "total_jobs": 1,
    "successful_jobs": 1,
    "failed_jobs": 0,
    "skipped_jobs": 0,
    "total_bytes_input": 8388608,
    "total_bytes_emitted": 3145728,
    "cooked_root": "./Cooked"
  },
  "jobs": [
    {
      "id": "job-model",
      "type": "fbx",
      "source": "assets/character.fbx",
      "status": "SUCCESS",
      "created_ms": 1674327045000,
      "started_ms": 1674327045050,
      "completed_ms": 1674327050234,
      "diagnostics": [],
      "telemetry": {
        "io_ms": 150,
        "decode_ms": 400,
        "load_ms": 300,
        "cook_ms": 2500,
        "emit_ms": 1234,
        "finalize_ms": 100,
        "total_ms": 5184,
        "input_bytes": 8388608,
        "emitted_bytes": 3145728
      },
      "assets": [
        {
          "asset_type": "geometry",
          "asset_key": "geo-character-uuid",
          "virtual_path": "/cooked/geometry/character.ogeo",
          "descriptor_path": "geometry/character.ogeo",
          "descriptor_size_bytes": 4096,
          "metadata": {
            "vertex_count": 450000,
            "index_count": 1350000,
            "lod_count": 3,
            "submesh_count": 5,
            "has_normals": true,
            "has_tangents": true
          },
          "resource_dependencies": [
            {
              "resource_type": "texture",
              "resource_index": 0,
              "name": "character_albedo",
              "virtual_path": "/cooked/resources/textures/character_albedo.bin",
              "size_bytes": 2097152,
              "hash_sha256": "abc123def456..."
            },
            {
              "resource_type": "texture",
              "resource_index": 1,
              "name": "character_normal",
              "virtual_path": "/cooked/resources/textures/character_normal.bin",
              "size_bytes": 2097152,
              "hash_sha256": "xyz789asd123..."
            }
          ],
          "asset_dependencies": [],
          "hashes": {
            "descriptor_sha256": "abc123def456..."
          }
        },
        {
          "asset_type": "material",
          "asset_key": "mat-character-skin-uuid",
          "virtual_path": "/cooked/materials/character_skin.omat",
          "descriptor_path": "materials/character_skin.omat",
          "descriptor_size_bytes": 2048,
          "metadata": {
            "material_type": "pbr_metallic_roughness",
            "texture_binding_count": 3,
            "packing_flags": ["ORM"]
          },
          "resource_dependencies": [
            {
              "resource_type": "texture",
              "resource_index": 2,
              "name": "character_orm",
              "virtual_path": "/cooked/resources/textures/character_orm.bin",
              "size_bytes": 1048576,
              "hash_sha256": "def456xyz789..."
            }
          ],
          "asset_dependencies": [],
          "hashes": {
            "descriptor_sha256": "xyz789asd123..."
          }
        }
      ],
      "resource_count": 3
    }
  ],
  "diagnostics": []
}
```

**Acceptance Criteria:**

- AC‑REPORT‑001: Report schema version matches schema used for validation.
- AC‑REPORT‑002: All numeric timestamps are in milliseconds since epoch.
- AC‑REPORT‑003: Asset metadata includes all format‑specific fields for each type.
- AC‑REPORT‑004: SHA‑256 hashes match actual file contents when verified.
- AC‑REPORT‑005: Report can be parsed by jq and other standard JSON tools.

---

## 8. Implementation Roadmap and Phases

### Phase 1: Stabilization (Milestone A)

**Objectives:** Normalize current CLI and execution model.

**Work Items:**

1. Add all global options from REQ‑CLI‑001 to Clap CLI builder.
2. Standardize exit codes across all commands (REQ‑CLI‑200).
3. Implement `--help` output with option tables and examples.
4. Add telemetry collection to `ImportRunner` for all job types.
5. Add basic text‑mode progress output (REQ‑EXE‑031) when TUI is disabled.
6. Unit tests for CLI parsing and exit code paths.

**Deliverables:**

- `--help` displays all global and command options
- Exit codes 0, 2, 3, 4, 5 are correctly emitted
- Text progress output is machine‑parseable
- Telemetry is captured for all job types

### Phase 2: New Job Types (Milestone B)

**Objectives:** Implement FBX and glTF import commands with manifest support.

**Work Items:**

1. Create `FbxCommand` implementing `ImportCommand` interface.
2. Create `GltfCommand` implementing `ImportCommand` interface.
3. Map CLI options to `ImportOptions` for both commands (REQ‑FBX‑001, REQ‑GLTF‑001).
4. Extend manifest schema to support `job_type` and per‑type configuration blocks (REQ‑MF‑030, REQ‑MF‑040, REQ‑MF‑050).
5. Update `ImportManifestLoader` to validate job type values.
6. Wire new commands into `BuildCli` framework.
7. Integration tests for FBX and glTF sample files.

**Deliverables:**

- `texture`, `fbx`, `gltf` commands are operational
- Manifest with `job_type` field loads and executes correctly
- Per‑type configuration blocks are parsed and applied

### Phase 3: Execution and UX (Milestone C)

**Objectives:** Enhance execution model, TUI, and job dependencies.

**Work Items:**

1. Implement job dependency graph resolution in batch runner (REQ‑EXE‑003, REQ‑MF‑023).
2. Add TUI display for real‑time job status (REQ‑EXE‑030).
3. Implement fail‑fast cancellation with 100ms timeout (REQ‑EXE‑005).
4. Add resource class throttling for CPU vs IO‑heavy jobs.
5. Improve non‑TUI text output formatting (REQ‑EXE‑031).
6. Add dry‑run manifest resolution and JSON output.
7. Integration tests for dependency ordering, TUI rendering, and fail‑fast.

**Deliverables:**

- Batch mode with 50+ jobs executes with real‑time progress
- Dependency graphs are validated and resolved
- Fail‑fast stops execution within 100ms
- TUI displays job grid with status and phase
- Text mode output is structured with timestamps

### Phase 4: Reporting and Automation (Milestone D)

**Objectives:** Stable report schema, JSON output for CI integration.

**Work Items:**

1. Implement report JSON schema with per‑job and asset metadata (REQ‑REPORT‑001 through REQ‑REPORT‑043).
2. Collect per‑job telemetry for all phases (REQ‑EXE‑010, REQ‑REPORT‑020).
3. Emit ASSET metadata for geometry, materials, and scenes (REQ‑REPORT‑040 through REQ‑REPORT‑042).
4. Reference RESOURCE metadata (textures, buffers) via `resource_dependencies` arrays (REQ‑REPORT‑031, REQ‑REPORT‑032).
5. Add SHA‑256 hashing for verification (REQ‑REPORT‑030).
6. Generate reports from `texture` and `batch` commands.
7. Create sample reports and validation tooling.
8. Integration tests for report generation and parsing.

**Deliverables:**

- JSON reports generated for all job types
- ASSET metadata (geometry, material, scene) includes format‑specific fields
- RESOURCE references include path, size, and hash (not blob data)
- SHA‑256 hashes are computed and included
- Reports pass jq schema validation
- CI integration examples provided

---

## 9. Acceptance and Validation Strategy

### 9.1 Unit Testing

**REQ‑TEST‑001**: CLI option parsing SHALL be tested with:

- Valid option combinations
- Invalid/missing required options
- Default value application
- Preset override precedence (REQ‑TEX‑020)

**REQ‑TEST‑002**: Manifest loading and validation SHALL test:

- Schema version mismatches
- Invalid job types
- Circular dependencies
- Relative path resolution

### 9.2 Integration Testing

**REQ‑TEST‑010**: End‑to‑end import workflows SHALL be tested with:

- Single texture import to verify CLI binding to `TextureImportSettings`
- Batch manifest with 50+ jobs to verify concurrency and dependency ordering
- FBX and glTF files with embedded textures to verify all emitted assets
- Fail‑fast termination within 100ms
- Report generation with complete asset metadata

**REQ‑TEST‑011**: Cross‑platform compatibility SHALL be verified:

- Windows (MSVC compiler) with absolute and relative paths
- Linux (GCC/Clang) with symlinks and case‑sensitive paths
- macOS with resource forks and extended attributes in source files

### 9.3 Regression Testing

**REQ‑TEST‑020**: The tool SHALL be tested against regression cases:

- Previously‑working manifests from version 0.1
- Sample texture intents from `TextureImportPresets`
- FBX files with various unit scales and coordinate conversions
- glTF files with embedded and external textures

---

## 10. Known Limitations and Future Work

### 10.1 Current Limitations

1. **TUI Platform Support**: Curses integration is currently tested on Windows (WinCon) and Linux. macOS support via ncurses is planned.
2. **Resource Throttling**: CPU vs IO‑heavy job classification is not yet implemented; all jobs share a single in‑flight pool.
3. **Pause/Resume**: Interactive TUI pause/resume is not yet implemented (Phase 3 enhancement).
4. **Config File**: No support for persistent settings file (e.g., `~/.importtool.toml`); all options must be CLI or manifest‑based.

### 10.2 Future Enhancements

1. **Advanced Job Filtering**: Tag‑based job filtering in manifest loading (e.g., `--only-tags production`).
2. **Incremental Imports**: Diff‑based imports to skip unchanged sources.
3. **Plugin System**: Support for custom importer implementations beyond texture/FBX/glTF.
4. **Report Webhooks**: HTTP callback hooks for CI/CD integration (e.g., Slack notifications on failure).

---

## 11. References

- [Oxygen Content Module](../README.md) - Core import pipeline
- [Import Pipeline API](../../Public/) - `ImportRequest.h`, `ImportOptions.h`, `AsyncImportService.h`
- [Texture Import Types](../Types/TextureImportTypes.h) - Type definitions and constants
- [Manifest Schema](./ImportManifest_schema.h) - JSON Schema (Draft‑7) validator
- [Clap Documentation](https://docs.rs/clap/) - CLI framework and macros
