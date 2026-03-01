# Oxygen.Cooker.ImportTool

`Oxygen.Cooker.ImportTool` imports source assets into Oxygen loose-cooked layout.

Supported import kinds:
- `texture`
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

| Option | Meaning |
| --- | --- |
| `-q`, `--quiet` | Suppress non-error output |
| `--diagnostics-file <path>` | Reserved diagnostics output path (currently parsed but not emitted) |
| `-o`, `--cooked-root <path>` | Default cooked root fallback |
| `--fail-fast` | Stop batch processing on first failure |
| `--no-color` | Disable ANSI color |
| `--no-tui` | Disable TUI; force text progress |
| `--theme <plain\|dark\|light>` | Help/output theme |
| `--thread-pool-size <n>` | Override import service worker count |
| `--concurrency <spec>` | Override pipeline concurrency (`t,b,m,h,g,s` as `workers/queue`) |

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

### `fbx`

Imports one FBX scene.

Required:
- positional `source`

Common options:
- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Scene controls:
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

Options are the same shape as `fbx`.

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

Rules:
- `compile=true` with `script-storage=external` is rejected.
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
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
- installed for users as: `schemas/oxygen.import-manifest.schema.json`
- installed for users as: `schemas/oxygen.input.schema.json`
- installed for users as: `schemas/oxygen.input-action.schema.json`
- installed for users as: `schemas/oxygen.physics-sidecar.schema.json`

Editor association examples (VSCode):

Repository checkout:

```json
{
  "json.schemas": [
    { "fileMatch": ["import-manifest*.json"], "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json" },
    { "fileMatch": ["*.input.json"], "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json" },
    { "fileMatch": ["*.input-action.json"], "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json" },
    { "fileMatch": ["*.physics-sidecar.json"], "url": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json" }
  ]
}
```

Installed package layout:

```json
{
  "json.schemas": [
    { "fileMatch": ["import-manifest*.json"], "url": "./schemas/oxygen.import-manifest.schema.json" },
    { "fileMatch": ["*.input.json"], "url": "./schemas/oxygen.input.schema.json" },
    { "fileMatch": ["*.input-action.json"], "url": "./schemas/oxygen.input-action.schema.json" },
    { "fileMatch": ["*.physics-sidecar.json"], "url": "./schemas/oxygen.physics-sidecar.schema.json" }
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
      "params": [
        { "key": "speed", "type": "float", "value": 1.0 }
      ]
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

Imports scene physics bindings as a standalone `.physics` descriptor.

Input modes (exactly one required):
- positional `source` (JSON sidecar file), or
- `--bindings-inline '<json>'`

`--bindings-inline` accepts either:
- a JSON object with `bindings` object (`{ "bindings": { ... } }`)
- or a bare bindings object (`{ "rigid_bodies": [...], ... }`)

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
        "shape_virtual_path": "/.cooked/PhysicsShapes/box.ocshape",
        "material_virtual_path": "/.cooked/PhysicsMaterials/default.opmat",
        "body_type": "dynamic"
      }
    ]
  }
}
```

Physics sidecar import emits a `.physics` descriptor and does not patch the
scene descriptor.

### `batch`

Runs manifest jobs.

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
    "texture": { "output": "..." },
    "scene": { "output": "..." },
    "script": { "output": "...", "compile": true, "script_storage": "embedded" },
    "scripting_sidecar": { "output": "...", "target_scene_virtual_path": "/Scenes/MyScene.oscene" },
    "physics_sidecar": { "output": "...", "target_scene_virtual_path": "/Scenes/MyScene.oscene" }
  },
  "jobs": []
}
```

Job rules:
- each job requires `type`
- non-sidecar jobs require `source`
- `input` jobs require:
  - `id`
  - `source`
  - optional `depends_on` (array of job ids)
  - allowed keys are exactly: `id`, `type`, `source`, `depends_on`
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
          "params": [
            { "key": "speed", "type": "float", "value": 1.0 }
          ]
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
            "shape_virtual_path": "/PhysicsShapes/backpack_body.ocshape",
            "material_virtual_path": "/PhysicsMaterials/default.opmat",
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
