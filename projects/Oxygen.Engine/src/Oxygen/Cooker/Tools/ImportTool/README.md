# Oxygen.Cooker.ImportTool

`Oxygen.Cooker.ImportTool` imports source assets into Oxygen loose-cooked
layout.

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

Single-job commands (`texture`, `fbx`, `gltf`, `input`, `script`,
`script-sidecar`, `physics-sidecar`):

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

Texture options include intent/format/mips/cubemap/decode controls. Run
`texture --help` for the full list.

An import manifest can set `source_key` to a canonical UUIDv7 at the top level,
in `defaults`, or on a job; the closest setting wins. Jobs writing the same
physical root must agree. A caller creating a new root generation supplies a
fresh key, including when staging was seeded from an older index. The native
writer reidentifies the root while retaining unchanged indexed content.

For a named texture consumed by material or scene references, use a
`texture-descriptor` job with `virtual_path` in its descriptor, for example
`"virtual_path": "/Content/Textures/Meter.otex"` under a `/Content` virtual
mount. The producer writes that exact descriptor location and keeps texture
tables/data in the configured resource directory. The path is validated before
import work; both normal and fallback output use it.

### `fbx`

Imports one FBX scene.

Required:

- positional `source`, or `--record <path>` to replay a model import record
- `--virtual-path <path>` with `source`: the model's identity, the virtual path
  of its source file (`/Content/Ship/Ship.glb`). Its materials, geometry,
  textures and scene are named flat under it (`/Content/Ship/Ship.glb/Paint.omat`,
  `/Content/Ship/Ship.glb/Ship.oscene`); its buffers are files beside its
  geometry (`Ship.glb/Hull.lod0.vertex.obuf`).
- an import without a record (`--output`) gets a fresh source identity and
  reports `import.source_identity_missing`: its node and surface IDs change on
  every import

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
- `--bake-transforms` (bake node transforms into meshes; off by default, so
  node pivots stay)
- `--normals <none|preserve|generate|recalculate>`
- `--tangents <none|preserve|generate|recalculate>`
- `--prune-nodes <keep|drop-empty>`
- `--up-axis <from-file|y|z>` (`fbx` only): the axes the source is read in;
  the import always produces Oxygen's +Z up. `from-file` (default) takes the
  file's axes, or FBX's default right-handed Y up when they are incomplete
  (`fbx.axis_unknown`); `y` and `z` read the source as right-handed Y up or Z
  up, whatever the file states
- `--smoothing-angle <degrees>` (default 60): faces that meet at a larger angle
  keep separate normals wherever the import generates or recalculates normals
- `--no-import-lights`, `--no-import-cameras`
- `--light-intensity-scale <float>` (default 1): multiplies every imported
  light's intensity
- `--max-texture-size <pixels>`: a larger texture drops its largest mips before
  compression

The manifest job and the model import record also hold settings keyed by parts
of the file, by the names `analyze-sources` reports:

- `material_remap`: a material's name to the virtual path of an existing
  material its surfaces use instead; the replaced material is not produced. A
  target that does not exist warns (`import.material_remap_target_missing`) and
  the source material is produced.
- `texture_overrides`: texture settings by texture name, over the job's.
- `geometry_defaults`: by geometry anchor, the asset defaults the geometry
  carries: `lod_policy`, and by surface ID `visible` and `casts_shadows`.

A key the file no longer has is kept and warns (`import.setting_target_missing`).
`up_axis` on a glTF job warns (`import.setting_not_applicable`) and is ignored.
A glTF primitive without normals stays faceted, as its specification requires,
unless normals are recalculated; an FBX mesh without normals takes its smoothing
groups when it has them, else the smoothing angle. An FBX camera that states its film height and
focal length takes its vertical field of view from them.

### `gltf`

Imports one glTF/GLB scene.

Required:

- positional `source`
- `--virtual-path <path>`, as for `fbx`

Options are the same shape as `fbx`. A record can be replayed without supplying
`source`.

A glTF file can hold several scenes; one becomes the model scene.
`--gltf-scene <selector>` (manifest `gltf_scene`) names it by its name, or by
`#N` for the scene at index N; without it the import takes the file's default
scene, else the first. A selector the file lacks warns
(`import.gltf_scene_missing`) and the default applies; every scene not imported
is reported with `import.source_feature_dropped`. Geometry, materials and
textures are imported for the whole file whichever scene is chosen. A document
that fails `cgltf_validate` (`gltf.validation_failed`), or whose model scene
reaches one node twice (`import.gltf_node_reached_twice`), is refused.

`analyze-sources` reports, for each `gltf` or `fbx` job, the source's format
(`gltf`, `glb` or `fbx`) and the version it states (glTF `asset.version`, the
FBX file version such as `7.4`), its unit, axes and handedness, node and mesh counts, the names of the materials and
textures the import produces, and for glTF each scene with its selector. It
reads a `.glb`'s JSON chunk only, never its binary chunk.

### Model import records

A model's import state lives in its model import record,
`<content_root>/imports/<source_identity>.import.json`
(`oxygen.model-import.schema.json`), which the editor reads and writes too: the
source, the model's virtual path, its settings, and what earlier imports
established (source identity, surface provenance, node continuity, published
nodes, source fingerprint). The source identity names the record and never
changes; node and surface IDs derive from it, so they survive moving or
renaming the model.

```powershell
Oxygen.Cooker.ImportTool gltf model.glb --content-root H:/Game/Content --virtual-path /Content/model.glb
Oxygen.Cooker.ImportTool fbx model.fbx --content-root H:/Game/Content --record H:/Game/Content/imports/<source_identity>.import.json
Oxygen.Cooker.ImportTool fbx --record H:/Game/Content/imports/<source_identity>.import.json
Oxygen.Cooker.ImportTool fbx --recipe model.recipe.json --record H:/Game/Content/imports/<source_identity>.import.json --content-root H:/Game/Content
```

With `--content-root` and a source but no `--record`, the tool finds the
record; the first step that matches decides:

| Step | Finds                                                                | Result                                                                                                                                     |
| ---- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| 1    | A record with this virtual path                                      | That record; a different source is rebound to this file (`import.source_rebound`)                                                          |
| 2    | A record whose source is this file, under another virtual path       | A new record, another model from the same file (`import.source_has_model`)                                                                 |
| 3    | A record whose source is missing and whose fingerprint is the file's | That record, its source and virtual path rewritten (`import.source_relocated`); several match: none (`import.source_relocation_ambiguous`) |
| 4    | Nothing                                                              | A new record, with the hints `import.source_copy` and `import.source_relocation_candidate`                                                 |

Two records with one virtual path, or one source identity, are refused.
`--record <record> <source>` rebinds that record to the source. A replay whose
source is gone fails with `import.source_missing`, which names the command that
rebinds it. Record imports derive their output from Content: do not combine
them with `--output`.

The publisher commits the import's surface provenance, published nodes and
source fingerprint to the record together with the validated immutable
generation, which it selects in
`Content/.cooked/imports/<source_identity>/head.json`. Failed or interrupted
attempts leave the record and the selection unchanged. External raw sources are
read only. The
[pipeline owner](../../Docs/Import/async_import_pipeline_v2.md#model-import-publication)
defines publication and lifetime.

`--recipe` saves the settings of a single-job gltf or fbx manifest, without
`defaults`, into the record. Relative sources resolve beside that manifest. The
command validates format and conflicting options before changing the record.
Recipe application and record replay accept reporting and execution options;
content overrides require a direct source or an updated recipe. Worker budgets
belong to `--thread-pool-size`/`--concurrency`, outside records.

Reclaim unused generations without another import:

```powershell
Oxygen.Cooker.ImportTool reclaim --record H:/Game/Content/imports/<source_identity>.import.json
```

This uses the publisher's record lock and generation leases. It keeps the
selected generation and any active reader/writer generations, and leaves the
record and original sources unchanged. Repeat after readers release their old
generations. The
[Content workflow](../../../../../Examples/Content/README.md) owns the example
import, recook, packaging and cleanup commands.

### Model jobs in manifests

A manifest job `{ "type": "model", "record": "imports/<source_identity>.import.json" }`
runs a record as the gltf or fbx job its source names, with the record's
settings, virtual path and identity. The record resolves as job sources do,
against `--root` or beside the manifest. The job may add `id`, `depends_on`,
`output`, `layout`, `cooked_context_roots` and `source_key`; settings stay in
the record. Jobs carry no source identity or surface provenance of their own.

A batch never writes records. Each successful model job's report holds
`model_import`, the record fields the import established
(`surface_provenance`, `published_nodes` and `source_fingerprint`); the host
commits them with its own publication, as the editor does.

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
- `--script-source-root <directory>`: authored content root, required for
  external storage

Rules:

- `compile=true` with `script-storage=external` embeds the bytecode and records
  the source path: the runtime prefers the source when a script source root
  contains it and runs the bytecode otherwise.
- External source paths are stored relative to `--script-source-root`; the
  source file must be inside that directory. In batch manifests, set
  `source_root` on the script job or `defaults.script`. Relative roots resolve
  from the manifest directory, independently of the input `--root` and cooked
  output location.
- In this tool, script compile is wired through Luau compiler callback.

Script import writes script descriptors (`*.oscript`) and script payload tables:

- `scripts.table`
- `scripts.data`

### `input`

Imports one input source (`*.input.json`): its actions and mapping contexts,
each named under the source's identity
(`/Content/Input/Player.input/Jump.oiact`). A mapping names an action of the
same source by its name, and an action of another source by its virtual path.

Required:

- positional `source`
- `--virtual-path <path>`: the source's identity, its path without the authoring
  `.json` (`/Content/Input/Player.input`)

Optional:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`
- `--content-hashing <true|false>`

Shipped JSON schemas:

- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json`
- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json`
- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json`
- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`
- source-of-truth: `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
- source-of-truth:
  `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
- installed for users as: `schemas/oxygen.import-manifest.schema.json`
- installed for users as: `schemas/oxygen.texture-descriptor.schema.json`
- installed for users as: `schemas/oxygen.material-descriptor.schema.json`
- installed for users as: `schemas/oxygen.geometry-descriptor.schema.json`
- installed for users as: `schemas/oxygen.scene-descriptor.schema.json`
- installed for users as: `schemas/oxygen.input.schema.json`
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
      "fileMatch": ["*.physics-sidecar.json"],
      "url": "./schemas/oxygen.physics-sidecar.schema.json"
    }
  ]
}
```

Slot names:

- Canonical runtime slot names are accepted (for example: `Up`, `RightCtrl`,
  `PrintScreen`).
- Authoring aliases are also accepted and normalized during import (for example:
  `UpArrow` -> `Up`, `RightControl` -> `RightCtrl`, `Print` -> `PrintScreen`).

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

Supported param types: `bool`, `int32`, `float`, `string`, `vec2`, `vec3`,
`vec4`.

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

### `remove-cooked`

Removes cooked assets and auxiliary files from a loose cooked root without
cooking, for example the outputs of a source that was deleted or renamed.

```powershell
Oxygen.Cooker.ImportTool remove-cooked H:/Game/.cooked/Content --input removal.json
```

The request lists the virtual paths of the assets and the root-relative paths of
the auxiliary files to remove, and may name the identity the updated index
takes:

```json
{
  "source_key": "0199a6c4-1f2e-7b3a-9c4d-5e6f7a8b9c0d",
  "virtual_paths": ["/Content/Materials/Blue.omat"],
  "auxiliary_files": []
}
```

Every listed entry must be in the index. The index is published first and the
files are deleted after it, so the index never lists a missing file. Resource
table entries that removed assets used stay in their tables, as they do when a
cook replaces an asset; a rebuild of the root compacts them.

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
- `--cooked-context-root <path>` (repeatable): a loose cooked root or pak file
  that resolves references for every scene, geometry and scripting sidecar job,
  beneath the context roots the manifest names. Pass the engine content pak
  (`share/oxygen/engine.pak` in an SDK) to reference `/Engine` assets.
- `--max-in-flight-jobs <n>`

## Manifest Format

Top-level fields:

```json
{
  "version": 1,
  "output": "F:/absolute/cooked/root",
  "thread_pool_size": 8,
  "max_in_flight_jobs": 16,
  "job_admission_capacity": 64,
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
      "virtual_mount_root": "/Content"
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

Execution settings are optional; the schema states each default.
`thread_pool_size` and `max_in_flight_jobs` default to the processor count,
`job_admission_capacity` to 64, and each `concurrency` pipeline's `workers`
and `queue_capacity` to the values of `ImportConcurrency.h`. A pipeline may set
one of the two and leave the other at its default.

Job rules:

- each job requires `type`
- non-sidecar jobs require `source`
- a job that cooks one asset (`material-descriptor`, `geometry-descriptor`,
  `scene-descriptor`, `script`, `texture`, `texture-descriptor`,
  `physics-material-descriptor`, `collision-shape-descriptor`) requires
  `virtual_path`, the asset's identity. Its descriptor is written at that path
  below the mount (`/.cooked/Materials/Wood.omat` lives at
  `Materials/Wood.omat`), and its key derives from it. A descriptor that states
  its own `virtual_path` must name the same one.
- an `input` job requires `virtual_path`, the input source's identity
  (`/.cooked/Input/Player.input`); each action and mapping context is named
  under it (`/.cooked/Input/Player.input/Jump.oiact`, written at
  `Input/Player.input/Jump.oiact`).
- a model job (`gltf`, `fbx`) requires `virtual_path`, the model's identity: the
  virtual path of its source file, ending in its extension
  (`/.cooked/Ship.glb`). Each material, geometry, texture and scene is named
  flat under it (`/.cooked/Ship.glb/Paint.omat`, written at
  `Ship.glb/Paint.omat`); the scene takes the model's stem (`Ship.oscene`).
- the single-file `texture`, `script` and `input` commands require
  `--virtual-path`, as do `gltf` and `fbx` without `--record`
- `texture-descriptor` jobs:
  - use `source` as descriptor JSON path
  - support texture tuning keys (`intent`, `mip_policy`, `output_format`, etc.)
    as manifest-level defaults/job overrides
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
  - resolve geometry/material/script/input/physics references from mounted
    cooked roots
- `input` jobs require:
  - `id`
  - `source`
  - `virtual_path`
  - optional `depends_on` (array of job ids): the jobs producing the actions of
    other sources this source maps
  - optional `output`
  - allowed keys are exactly: `id`, `type`, `source`, `virtual_path`,
    `depends_on`, `output`, `layout`, `source_key`
- layout overrides are shared across all importers via manifest `layout` or
  `defaults.layout`: `virtual_mount_root` and `resources_dir`. They place the
  resource tables; every descriptor is placed by its identity.
- `script-sidecar` requires exactly one of:
  - `source`
  - `bindings` (inline array)
- `script-sidecar` always requires `target_scene_virtual_path`
- `script-sidecar` accepts `cooked_context_roots`: published roots that resolve
  scripts and asset parameters cooked before this manifest
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
      "virtual_path": "/.cooked/backpack.glb",
      "name": "backpack_scene"
    },
    {
      "type": "script",
      "source": "Examples/Content/backpack_rotate.lua",
      "virtual_path": "/Descriptors/Scripts/backpack_rotate.oscript",
      "compile": true,
      "script_storage": "embedded",
      "name": "backpack_rotate"
    },
    {
      "type": "script-sidecar",
      "target_scene_virtual_path": "/.cooked/backpack.glb/backpack.oscene",
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
      "target_scene_virtual_path": "/.cooked/backpack.glb/backpack.oscene",
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

- Use the actual `scene` and `script` virtual paths emitted by
  import/report/index.
- `script_virtual_path` must resolve to a script asset.

## Reports and Exit Codes

Per-command `--report` and batch `--report` write JSON report output, version
`3`. Each job lists:

- `outputs`: every file the job wrote, as `{path, size_bytes}`, including shared
  tables and the index.
- `products`: what the job's source produced, as
  `{kind, virtual_path, descriptor_relative_path}`. An asset (`kind` `asset`)
  adds `asset_key` and its numeric `asset_type`. A texture descriptor (`kind`
  `texture`) has neither. A tool maps a source to its products, and a product to
  its source, from this list rather than from the cooked layout.

Current process exit codes:

- `0` success
- `1` invalid input/argument/configuration
- `2` runtime/import failure

## Tips

- Put global options before the command (`-o`, `--no-tui`, `--fail-fast`, etc.).
- Use `--no-tui` for CI/log pipelines.
- Use `--dry-run` with `batch` to validate manifests before execution.
- Run `<command> --help` for exhaustive option-level help.
