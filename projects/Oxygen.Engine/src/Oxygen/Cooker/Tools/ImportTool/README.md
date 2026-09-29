# Oxygen.Cooker.ImportTool

`Oxygen.Cooker.ImportTool` imports source assets into Oxygen loose-cooked layout.

Supported import kinds:

- `texture`
- `texture-descriptor` (manifest job type)
- `buffer-container` (manifest job type)
- `material-descriptor` (manifest job type)
- `geometry-descriptor` (manifest job type)
- `scene-descriptor` (manifest job type)
- `fbx`
- `gltf`
- `input`
- `script`
- `script-sidecar`
- `physics-sidecar`
- `batch` (manifest-driven)

## Quick Start

```bash
# Single script import (compiled + embedded)
Oxygen.Cooker.ImportTool -o F:/projects/MyGame/.cooked script \
  --compile \
  --compile-mode debug \
  --script-storage embedded \
  Examples/Content/scene_core_script.lua

# Batch import
Oxygen.Cooker.ImportTool batch --manifest F:/projects/MyGame/import-manifest.json
```

## Global Options

Global options are available for all commands.

| Option                         | Meaning                                                             |
| ------------------------------ | ------------------------------------------------------------------- |
| `-q`, `--quiet`                | Suppress non-error output                                           |
| `--diagnostics-file <path>`    | Reserved diagnostics output path (currently parsed but not emitted) |
| `-o`, `--cooked-root <path>`   | Default cooked root fallback                                        |
| `--fail-fast`                  | Stop batch processing on first failure                              |
| `--no-color`                   | Disable ANSI color                                                  |
| `--no-tui`                     | Disable TUI; force text progress                                    |
| `--theme <plain\|dark\|light>` | Help/output theme                                                   |
| `--thread-pool-size <n>`       | Override import service worker count                                |
| `--concurrency <spec>`         | Override pipeline concurrency (`t,b,m,h,g,s` as `workers/queue`)    |

Example concurrency override:

```bash
Oxygen.Cooker.ImportTool --concurrency "t:4/64,b:2/32,g:2/32,s:2/32" batch --manifest ...
```

## Output Root Resolution

Cooked root must resolve to an absolute path.

Single-job commands (`texture`, `fbx`, `gltf`, `input`, `script`, `script-sidecar`, `physics-sidecar`):

1. `-i`, `--output` (command-local)
2. global `-o`, `--cooked-root`

Batch (`--manifest`):

1. job `output`
2. `defaults.<type>.output`
3. top-level manifest `output`
4. global `-o`, `--cooked-root`

## Command Reference

### `texture`

Imports one texture.

Required:

- positional `source`

Common options:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Texture options include intent/format/mips/cubemap/decode controls.
Run `texture --help` for the full list.

For a named texture consumed by material or scene references, use a
`texture-descriptor` job with `virtual_path` in its descriptor, for example
`"virtual_path": "/Content/Textures/Meter.otex"` under a `/Content` virtual
mount. The producer writes that exact descriptor location and keeps texture
tables/data in the configured resource directory. The path is validated before
import work; both normal and fallback output use it. Omitting `virtual_path`
retains hashed naming for anonymous resources.

### `fbx`

Imports one FBX scene.

Required:

- positional `source`, or `--record <path>` to replay a retained recipe
- raw staging imports require `--material-slot-source-identity <uuid-v7>`

Common options:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Scene controls:

- `--material-slot-provenance <json-file>` (retained candidate from a prior import)
- `--no-import-textures`
- `--no-import-materials`
- `--no-import-geometry`
- `--no-import-scene`
- `--unit-policy <normalize|preserve|custom>`
- `--unit-scale <float>`
- `--no-bake-transforms`
- `--normals <none|preserve|generate|recalculate>`
- `--tangents <none|preserve|generate|recalculate>`
- `--prune-nodes <keep|drop-empty>`

### `gltf`

Imports one glTF/GLB scene.

Required:

- positional `source`

Options are the same shape as `fbx`. A retained record can be replayed without
supplying `source`.

### Retained model imports

Use the shared retained workflow for independently imported models:

```powershell
Oxygen.Cooker.ImportTool gltf model.glb --content-root H:/Game/Content
Oxygen.Cooker.ImportTool fbx model.fbx --content-root H:/Game/Content --record H:/Game/Content/imports/model.import.json
Oxygen.Cooker.ImportTool fbx --record H:/Game/Content/imports/model.import.json
Oxygen.Cooker.ImportTool fbx --recipe model.recipe.json --record H:/Game/Content/imports/model.import.json --content-root H:/Game/Content
```

The record owns recipe and slot identity; do not combine retained options with
`--output`, `--material-slot-source-identity` or `--material-slot-provenance`.
The native publisher selects a validated immutable generation and its provenance
in one atomic record replacement. Failed or interrupted attempts keep the prior
selection. External raw sources are read only. Records follow
`oxygen.retained-model-import.schema.json`; generated directories belong under
`Content/.cooked/imports`. The [pipeline owner](../../Docs/Import/async_import_pipeline_v2.md#retained-model-publication)
defines publication and lifetime.

`--recipe` applies a single-job native import manifest, including texture defaults
and mesh settings. Relative sources resolve beside that manifest. The command
validates format and conflicting options before changing the retained record.
Recipe application and record replay accept reporting and execution options;
content overrides require a direct source or an updated recipe. Worker budgets
belong to `--thread-pool-size`/`--concurrency`, outside retained recipes.

Reclaim unused generations without another import:

```powershell
Oxygen.Cooker.ImportTool reclaim --record H:/Game/Content/imports/model.import.json
```

This uses the retained publisher's record lock and generation leases. It keeps
the selected generation and any active reader/writer generations, and leaves the
authored record and original sources unchanged. Repeat after readers release
their old generations. The [Content workflow](../../../../../Examples/Content/README.md)
owns the example import, recook, packaging and cleanup commands.

### Staged model imports

Project transactions and SDK batches may use explicit output roots. Their host
owns publication of cooked output together with the returned provenance.

Keep the source identity with the source asset and reuse it on every reimport.
Successful single and batch job reports include `material_slot_provenance` when
the importer allocates slots. Preserve that object in the host-owned source metadata;
pass it through the CLI file option or a manifest's `material_slot_provenance`
property. The file contains the object itself, not the surrounding job report.
Failed or canceled jobs do not publish a candidate. Clearing cooked output does
not erase this retained authoring data. The object follows the installed
`oxygen.material-slot-provenance.schema.json` schema.

Manifest scene defaults and individual FBX/glTF jobs accept
`material_slot_source_identity` and `material_slot_provenance`. Job values
override defaults. Give independent source assets independent identities;
do not derive identities from machine-local paths.

### `script`

Imports one script asset.

Required:

- positional `source`

Options:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`
- `--compile <true|false>`
- `--compile-mode <debug|optimized>`
- `--script-storage <embedded|external>`
- `--script-source-root <directory>`: authored content root, required for external storage

Rules:

- `compile=true` with `script-storage=external` is rejected.
- External source paths are stored relative to `--script-source-root`; the source file
  must be inside that directory. In batch manifests, set `source_root` on the
  script job or `defaults.script`. Relative roots resolve from the manifest
  directory, independently of the input `--root` and cooked output location.
- In this tool, script compile is wired through Luau compiler callback.

Script import writes script descriptors (`*.oscript`) and script payload tables:

- `scripts.table`
- `scripts.data`

### `input`

Imports one input authoring document (`*.input.json` or `*.input-action.json`).

Required:

- positional `source`

Optional:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Shipped JSON schemas:

- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
- installed for users as: `schemas/oxygen.import-manifest.schema.json`
- installed for users as: `schemas/oxygen.texture-descriptor.schema.json`
- installed for users as: `schemas/oxygen.material-descriptor.schema.json`
- installed for users as: `schemas/oxygen.geometry-descriptor.schema.json`
- installed for users as: `schemas/oxygen.scene-descriptor.schema.json`
- installed for users as: `schemas/oxygen.input.schema.json`
- installed for users as: `schemas/oxygen.input-action.schema.json`
- installed for users as: `schemas/oxygen.physics-sidecar.schema.json`

Editor association examples (VSCode):

Repository checkout:

```json
{
  "json.schemas": [
    {
      "fileMatch": ["import-manifest*.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json"
    },
    {
      "fileMatch": ["*.texture.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.material.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.geometry.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.scene.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.input.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json"
    },
    {
      "fileMatch": ["*.input-action.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json"
    },
    {
      "fileMatch": ["*.physics-sidecar.json"],
      "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json"
    }
  ]
}
```

Installed package layout:

```json
{
  "json.schemas": [
    {
      "fileMatch": ["import-manifest*.json"],
      "url": "./schemas/oxygen.import-manifest.schema.json"
    },
    {
      "fileMatch": ["*.texture.json"],
      "url": "./schemas/oxygen.texture-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.material.json"],
      "url": "./schemas/oxygen.material-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.geometry.json"],
      "url": "./schemas/oxygen.geometry-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.scene.json"],
      "url": "./schemas/oxygen.scene-descriptor.schema.json"
    },
    {
      "fileMatch": ["*.input.json"],
      "url": "./schemas/oxygen.input.schema.json"
    },
    {
      "fileMatch": ["*.input-action.json"],
      "url": "./schemas/oxygen.input-action.schema.json"
    },
    {
      "fileMatch": ["*.physics-sidecar.json"],
      "url": "./schemas/oxygen.physics-sidecar.schema.json"
    }
  ]
}
```

Slot names:

- Canonical runtime slot names are accepted (for example: `Up`, `RightCtrl`, `PrintScreen`).
- Authoring aliases are also accepted and normalized during import (for example: `UpArrow` -> `Up`, `RightControl` -> `RightCtrl`, `Print` -> `PrintScreen`).

### `script-sidecar`

Imports scene scripting bindings.

Input modes (exactly one required):

- positional `source` (JSON sidecar file), or
- `--bindings-inline '<json>'`

`--bindings-inline` accepts either:

- a JSON array of binding rows (`[ ... ]`)
- or a JSON object with `bindings` array (`{ "bindings": [ ... ] }`)

Required:

- `--target-scene-virtual-path <canonical-virtual-path>`

Optional:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Canonical virtual-path requirements:

- starts with `/`
- no backslashes
- no `//`
- no trailing slash (except root)
- no `.` or `..` segments

Sidecar payload shape:

```json
{
  "bindings": [
    {
      "node_index": 0,
      "slot_id": "main",
      "script_virtual_path": "/Descriptors/Scripts/my_script.oscript",
      "execution_order": 0,
      "params": [{ "key": "speed", "type": "float", "value": 1.0 }]
    }
  ]
}
```

Supported param types: `bool`, `int32`, `float`, `string`, `vec2`, `vec3`, `vec4`.

Sidecar import writes script-binding payload tables:

- `script-bindings.table`
- `script-bindings.data`

It also patches scene scripting components for the target scene.

### `physics-sidecar`

Imports scene physics bindings as a standalone `.opscene` descriptor.

Input modes (exactly one required):

- positional `source` (JSON sidecar file), or
- `--bindings-inline '<json>'`

`--bindings-inline` accepts only a canonical sidecar object with a top-level
`bindings` object (`{ "bindings": { ... } }`).

Required:

- `--target-scene-virtual-path <canonical-virtual-path>`

Optional:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Canonical virtual-path requirements:

- starts with `/`
- no backslashes
- no `//`
- no trailing slash (except root)
- no `.` or `..` segments

Minimal payload example:

```json
{
  "bindings": {
    "rigid_bodies": [
      {
        "node_index": 0,
        "shape_ref": "/.cooked/Physics/Shapes/box.ocshape",
        "material_ref": "/.cooked/Physics/Materials/default.opmat",
        "body_type": "dynamic"
      }
    ]
  }
}
```

Physics sidecar import emits a `.opscene` descriptor and does not patch the
scene descriptor.

### `batch`

Runs manifest jobs.

Validation errors always fail the batch, even when valid jobs continue without
`--fail-fast`. Human and JSON summaries include `validation_errors` separately
from the execution results of prepared jobs.

Required:

- `-m`, `--manifest <path>`

Optional:

- `--root <path>` (base for resolving relative `source` values)
- `--dry-run`
- `--report <path>`
- `--max-in-flight-jobs <n>`

## Manifest Format

Top-level fields:

```json
{
  "version": 1,
  "output": "F:/absolute/cooked/root",
  "thread_pool_size": 8,
  "max_in_flight_jobs": 16,
  "concurrency": {
    "texture": { "workers": 4, "queue_capacity": 64 },
    "buffer": { "workers": 2, "queue_capacity": 32 },
    "material": { "workers": 2, "queue_capacity": 32 },
    "mesh_build": { "workers": 2, "queue_capacity": 32 },
    "geometry": { "workers": 2, "queue_capacity": 32 },
    "scene": { "workers": 2, "queue_capacity": 32 }
  },
  "defaults": {
    "layout": {
      "input_subdir": "Input"
    },
    "texture": { "output": "..." },
    "scene": { "output": "..." },
    "script": {
      "output": "...",
      "compile": true,
      "script_storage": "embedded"
    },
    "material_descriptor": { "output": "...", "content_hashing": true },
    "scripting_sidecar": {
      "output": "...",
      "target_scene_virtual_path": "/Scenes/MyScene.oscene"
    },
    "physics_sidecar": {
      "output": "...",
      "target_scene_virtual_path": "/Scenes/MyScene.oscene"
    }
  },
  "jobs": []
}
```

Job rules:

- each job requires `type`
- non-sidecar jobs require `source`
- `texture-descriptor` jobs:
  - use `source` as descriptor JSON path
  - support texture tuning keys (`intent`, `mip_policy`, `output_format`, etc.) as manifest-level defaults/job overrides
  - reject non-texture keys (for example scene/script-specific keys)
- `material-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output`, `name`, and `content_hashing`
  - descriptor `textures.*.virtual_path` must resolve to mounted cooked roots
- `geometry-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output`, `name`, and `content_hashing`
- `scene-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output`, `name`, and `content_hashing`
  - resolve geometry/material/script/input/physics references from mounted cooked roots
- `input` jobs require:
  - `id`
  - `source`
  - optional `depends_on` (array of job ids)
  - optional `output`
  - allowed keys are exactly: `id`, `type`, `source`, `depends_on`, `output`
- layout overrides are shared across all importers via manifest `layout` or
  `defaults.layout` (for example `input_subdir`)
- `script-sidecar` requires exactly one of:
  - `source`
  - `bindings` (inline array)
- `script-sidecar` always requires `target_scene_virtual_path`
- `physics-sidecar` requires exactly one of:
  - `source`
  - `bindings` (inline object)
- `physics-sidecar` always requires `target_scene_virtual_path`
- duplicate `id`, missing dependency targets, and dependency cycles are rejected
- if a dependency job fails, all transitive dependents are skipped with
  `input.import.skipped_predecessor_failed`

Batch example with one output shared at manifest level:

```json
{
  "version": 1,
  "output": "F:/projects/DroidNet/projects/Oxygen.Engine/.cooked",
  "jobs": [
    {
      "type": "gltf",
      "source": "Examples/Content/backpack.glb",
      "name": "backpack_scene"
    },
    {
      "type": "script",
      "source": "Examples/Content/backpack_rotate.lua",
      "compile": true,
      "script_storage": "embedded",
      "name": "backpack_rotate"
    },
    {
      "type": "script-sidecar",
      "target_scene_virtual_path": "/Scenes/backpack.oscene",
      "bindings": [
        {
          "node_index": 0,
          "slot_id": "main",
          "script_virtual_path": "/Descriptors/Scripts/backpack_rotate.oscript",
          "execution_order": 0,
          "params": [{ "key": "speed", "type": "float", "value": 1.0 }]
        }
      ]
    },
    {
      "type": "physics-sidecar",
      "target_scene_virtual_path": "/Scenes/backpack.oscene",
      "bindings": {
        "rigid_bodies": [
          {
            "node_index": 0,
            "shape_ref": "/Physics/Shapes/backpack_body.ocshape",
            "material_ref": "/Physics/Materials/default.opmat",
            "body_type": "dynamic"
          }
        ]
      }
    }
  ]
}
```

Notes:

- Use the actual `scene` and `script` virtual paths emitted by import/report/index.
- `script_virtual_path` must resolve to a script asset.

## Reports and Exit Codes

Per-command `--report` and batch `--report` write JSON report output.

Current process exit codes:

- `0` success
- `1` invalid input/argument/configuration
- `2` runtime/import failure

## Tips

- Put global options before the command (`-o`, `--no-tui`, `--fail-fast`, etc.).
- Use `--no-tui` for CI/log pipelines.
- Use `--dry-run` with `batch` to validate manifests before execution.
- Run `<command> --help` for exhaustive option-level help.
