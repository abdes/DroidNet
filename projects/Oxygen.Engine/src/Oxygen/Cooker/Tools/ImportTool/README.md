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
  --mount Content=F:/projects/MyGame/Content \
  --compile \
  --compile-mode debug \
  --script-storage embedded \
  --virtual-path /Content/Scripts/scene_core_script.oscript \
  F:/projects/MyGame/Content/Scripts/scene_core_script.lua

# Batch import
Oxygen.Cooker.ImportTool -o F:/projects/MyGame/.cooked batch \
  --manifest F:/projects/MyGame/import-manifest.json
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
| `--max-source-bytes <n>`       | Override the most bytes a job reads from one source                 |
| `--concurrency <spec>`         | Override pipeline concurrency (`t,b,m,h,g,s` as `workers/queue`)    |

Example concurrency override:

```bash
Oxygen.Cooker.ImportTool --concurrency "t:4/64,b:2/32,g:2/32,s:2/32" batch --manifest ...
```

## Output Root Resolution

Cooked root must resolve to an absolute path. A manifest holds no absolute
path: its `output` is relative to the manifest's folder, and the cooked root of
a generated manifest is passed with `--cooked-root`.

Single-job commands (`texture`, `fbx`, `gltf`, `input`, `script`,
`script-sidecar`, `physics-sidecar`):

1. `-i`, `--output` (command-local)
2. global `-o`, `--cooked-root`

Batch (`--manifest`):

1. job `output`
2. `defaults.<type>.output`
3. top-level manifest `output`
4. global `-o`, `--cooked-root`

## Source mounts

A source is located through a mount table (see the
[source mounts design](../../../../../design/proposals/source-mounts/README.md)).
Single import commands take:

- `--mount <Name>=<dir>` (repeatable): declares a source mount.
- `--mounts <file>`: a mounts file applied above every `.mounts.json` found.
- `--root <dir>`: the project root; default the working directory.

The positional `source` must lie in a source mount, else the import fails with
`import.source_outside_mount`. The loose cooked index records each output's
source as its mount path, `/<Mount>/<path in mount>`.

## Command Reference

### `texture`

Imports one texture.

Required:

- positional `source`

Common options:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`

Texture options include intent/format/mips/cubemap/decode controls. Run
`texture --help` for the full list.

An import manifest can set `source_key` to a canonical UUIDv7 at the top level,
in `defaults`, or on a job; the closest setting wins. Jobs writing the same
physical root must agree. A caller creating a new root generation supplies a
fresh key, including when staging was seeded from an older index. The native
writer reidentifies the root while retaining unchanged indexed content.

For a named texture consumed by material or scene references, use a
`texture-descriptor` job whose `virtual_path` is the texture's identity, for
example `"virtual_path": "/Content/Textures/Meter.otex"` under a `/Content`
virtual mount. The descriptor never states an identity of its own. The producer
writes that exact descriptor location and keeps texture tables/data in the
configured resource directory. The path is validated before import work; both
normal and fallback output use it.

### `fbx`

Imports one FBX scene.

Required:

- positional `source`, or a model import record to replay (`--record <path>`,
  or `--root` with `--virtual-path`)
- `--virtual-path <path>` with `source`: the model's identity, the virtual path
  of its source file (`/Content/Ship/Ship.glb`). Its materials, geometry,
  textures and scene are named flat under it (`/Content/Ship/Ship.glb/Paint.omat`,
  `/Content/Ship/Ship.glb/Ship.oscene`); its buffers are files beside its
  geometry (`Ship.glb/Hull.lod0.vertex.obuf`).
- `--output <path>` with a record (`--record` or `--root`): the generations
  folder the tool owns and publishes into
- an import without a record (`--output`) gets a fresh source identity and
  reports `import.source_identity_missing`: its node and surface IDs change on
  every import

Common options:

- `-i`, `--output <path>`
- `--name <job-name>`
- `--report <path>`

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
- `--generate-lods`: each geometry with one LOD gets generated LODs,
  simplified from LOD 0; a geometry with authored LODs keeps them
- `--lod-levels <count>` (default 3, from 1 to 7): the LODs generated after
  LOD 0
- `--lod-reduction <percent>` (default 50, from 1 to 99): the triangles each
  generated LOD removes from the previous one
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

A model's import state lives in its model import record
(`oxygen.model-import.schema.json`), which the editor reads and writes too.
The record is the model's authored file: it lives in the model's mount at the
model's identity with the suffix `.import.json`, so the model
`/Content/Models/Ship.glb` has the record `Models/Ship.glb.import.json` in the
`Content` mount's folder. The identity is where the record lives; the record
never repeats it. It holds the source, the settings, and what earlier imports
established (source identity, surface provenance, node continuity, published
nodes, source fingerprint). The record holds the source identity, which never
changes; node and surface IDs derive from it, so they survive moving or
renaming the model.

The `source` is a file reference from the record, with the grammar of every
reference: relative within the model's mount (`Ship.glb` beside the record,
`../SourceMedia/DCC/Ship.glb`), the mount path of a file in another mount
(`/RawMedia/Cars/Truck.fbx`). One source may have several records, each its
own model.

Records are found through the mounts of the project at `--root` (the working
directory by default), with `--mount` and `--mounts` as for any import:

```powershell
Oxygen.Cooker.ImportTool gltf Ship.glb --root H:/Game --virtual-path /Content/Models/Ship.glb --output H:/Scratch/Generations
Oxygen.Cooker.ImportTool gltf --root H:/Game --virtual-path /Content/Models/Ship.glb --output H:/Scratch/Generations
Oxygen.Cooker.ImportTool fbx model.fbx --root H:/Game --record H:/Game/Content/Models/Ship.fbx.import.json --output H:/Scratch/Generations
Oxygen.Cooker.ImportTool fbx --recipe model.recipe.json --root H:/Game --output H:/Scratch/Generations
```

`--root` with `--virtual-path` names the record at that identity; without a
source it replays it. With a source and no `--record`, the tool finds the
record among the records of the bound `Source` mounts, matching sources by
mount path; the first step that matches decides:

| Step | Finds                                                                | Result                                                                                                                                          |
| ---- | -------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | The record at the identity                                           | That record; a different source is rebound to this file (`import.source_rebound`)                                                               |
| 2    | A record whose source is this file                                   | A new record at the identity, another model from the same file (`import.source_has_model`)                                                      |
| 3    | A record whose source is missing and whose fingerprint is the file's | That model, rebound to this file under its own identity (`import.source_relocated`); several match: none (`import.source_relocation_ambiguous`) |
| 4    | Nothing                                                              | A new record at the identity, with the hints `import.source_copy` and `import.source_relocation_candidate`                                      |

Two records with one source identity are refused. `--record <record> <source>`
rebinds that record to the source; a `--virtual-path` beside `--record` must be
that record's identity, since moving a record is a relocation, not an import.
A replay whose source is gone fails with `import.source_missing`, which names
the command that rebinds it. Record imports publish into `--output`, a
generations folder the tool owns; it is required, and never a project's own
output folder.

The publisher commits the import's surface provenance, published nodes and
source fingerprint to the record together with the validated immutable
generation, which it selects in
`<output>/<source_identity>/head.json`. Failed or
interrupted attempts leave the record and the selection unchanged. External raw sources are
read only. The
[pipeline owner](../../Docs/Import/async_import_pipeline_v2.md#model-import-publication)
defines publication and lifetime.

`--recipe` saves the settings of a single-job gltf or fbx manifest, without
`defaults`, into the record of the job's `virtual_path`. Its sources are mount
paths or project-relative paths. The command validates format and conflicting
options before changing the record.
Recipe application and record replay accept reporting and execution options;
content overrides require a direct source or an updated recipe. Worker budgets
belong to `--thread-pool-size`/`--concurrency`, outside records.

Reclaim unused generations without another import:

```powershell
Oxygen.Cooker.ImportTool reclaim --record H:/Game/Content/Models/Ship.glb.import.json --root H:/Game --output H:/Scratch/Generations
```

This uses the publisher's record lock and generation leases. It keeps the
selected generation and any active reader/writer generations, and leaves the
record and original sources unchanged. Repeat after readers release their old
generations. The
[examples project](../../../../../Examples/Showcase/README.md) owns the example
import, recook, packaging and cleanup commands.

### Model jobs in manifests

A manifest job `{ "type": "model", "virtual_path": "/Content/Models/Ship.glb" }`
runs the record at that identity, found through the manifest's mounts, as the
gltf or fbx job its source names, with the record's settings and identity. The
job may add `id`, `depends_on`, `output`, `cooked_context_roots` and
`source_key`; settings stay in the record. Jobs carry no source identity or
surface provenance of their own.

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
- `--compile <true|false>`
- `--compile-mode <debug|optimized>`
- `--script-storage <embedded|external>`

Rules:

- `compile=true` with `script-storage=external` embeds the bytecode and records
  the source path: the runtime prefers the source when its mount folder
  holds it and runs the bytecode otherwise.
- An external script stores its source's mount path,
  `/<Mount>/<path in mount>`; a source in no mount fails with
  `script.asset.external_source_unmounted`.
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

Reference requirements: the target scene is named by its virtual path, and
every reference in the payload is an absolute virtual path in another mount or
text relative to the folder of the target scene, resolved against the target
scene's identity. Relative text starts with one `./` or any number of `../`,
then one or more segments; it has no empty segment, no trailing slash, no
backslash and no later `.` or `..` segment.

Sidecar payload shape:

```json
{
  "bindings": [
    {
      "node_index": 0,
      "slot_id": "main",
      "script_virtual_path": "../Scripts/my_script.oscript",
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

Reference requirements: the target scene is named by its virtual path, and
every reference in the payload is an absolute virtual path in another mount or
text relative to the folder of the target scene, resolved against the target
scene's identity. Relative text starts with one `./` or any number of `../`,
then one or more segments; it has no empty segment, no trailing slash, no
backslash and no later `.` or `..` segment.

Minimal payload example:

```json
{
  "bindings": {
    "rigid_bodies": [
      {
        "node_index": 0,
        "shape_ref": "../Physics/Shapes/box.ocshape",
        "material_ref": "../Physics/Materials/default.opmat",
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
cook replaces an asset; the pak step compacts them, packing only the resources
its assets reach.

### `batch`

Runs manifest jobs.

Validation errors always fail the batch, even when valid jobs continue without
`--fail-fast`. Human and JSON summaries include `validation_errors` separately
from the execution results of prepared jobs.

Required:

- `-m`, `--manifest <path>` (repeatable)

Several manifests run as one batch with one mount table: `mount.name_conflict`
is reported before any job runs, a job cooking the same source to the same
identity is cooked once, and a repeated job id fails with
`batch.job_id_duplicate`.

Optional:

- `--root <path>` (the project root; replaces every manifest's `project_root`)
- `--mounts <file>` (a mounts file applied above every `.mounts.json` found)
- `--engine-content` (the manifests cook engine content)
- `--dry-run`
- `--report <path>`
- `--cooked-context-root <path>` (repeatable): a loose cooked root or pak file
  that resolves references for every scene, geometry and scripting sidecar job,
  beneath the context roots the manifest names. Pass the engine content pak
  (`share/oxygen/engine.pak` in an SDK) to reference `/Engine` assets.
- `--max-in-flight-jobs <n>`

`analyze-sources` also takes `--mounts`. It cooks nothing: for every job of
the manifest it reports the files the job reads, the assets it produces and
the references its authored files make, resolved as the cook resolves them,
and the refusals the reference rules make. A source that is not valid JSON, or
lacks what its job reads (a sidecar's bindings, a collision shape's
`material_ref`, a container's `buffers`), is refused with
`analysis.source_invalid`; a refused job makes the analysis incomplete and the
command exit non-zero. A physics or script sidecar references its target
scene, and its references resolve against the scene's identity; a physics
sidecar produces the scene's `.opscene`.

### `versions`

Prints the versions of what the tool cooks: every cooked format and asset, and
the revision of every importer, all from `PakFormatVersions.inc`. `--json`
prints one object with `program`, `version`, `formats`, `assets` and
`importers`, each from a name to its version. An importer's revision goes up
when its output for the same inputs changes while every format stays the same,
so a caller that reuses cooked content cooks it again.

## Manifest Format

Top-level fields:

```json
{
  "version": 1,
  "project_root": "..",
  "mounts": [{ "name": "Content", "kind": "Source", "path": "Content" }],
  "output": "../.cooked/Content",
  "thread_pool_size": 8,
  "max_in_flight_jobs": 16,
  "job_admission_capacity": 64,
  "max_source_bytes": 2147483647,
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
    "script": {
      "output": "...",
      "compile": true,
      "script_storage": "embedded"
    },
    "material_descriptor": { "output": "..." },
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

`max_source_bytes` is the most bytes a job reads from one source file, or one
range of it: the model and its external buffers, images, sidecars and
descriptors. Its default, 2,147,483,647 bytes (`kDefaultMaxSourceBytes` in
`ImportLimits.h`), is a provisional limit that stays within the image
decoders' input size. A larger source is refused before anything is
allocated, and its job fails with `import.source_too_large`, which names the
path, its size and the limit. A texture refused this way gets no stand-in
found by name and no placeholder. `--max-source-bytes` overrides the manifest.

`project_root` is relative to the manifest's folder (default: the folder);
`--root` replaces it. `mounts` lists the mounts: `name`, optional `kind`
(`Source`, the default, or `Cooked`, a cooked source: a loose cooked root or a `.pak`
file; another kind fails with `mount.kind_unknown`) and optional `path`,
relative to the project root. A mount without `path` is a placeholder a
`.mounts.json` binds. A bound `Cooked` mount is read as a cooked source: one that
cannot be read, whose one root is not its name, that holds more than one root
or a script with an external source warns with its
`mount.cooked_*` finding and is `Invalid`, never mounted. Every job reads
the valid cooked sources after its own output root and before its
`cooked_context_roots`. A cooked root
holds one mount, the one its identities name (`content::MountRootOf`); a job
whose identity names another mount is refused with `import.output_mount_mixed`.
Every path is relative; an absolute path in any
field is refused with `path.absolute_committed`.

Job rules:

- each job requires `type`
- non-sidecar jobs other than `model` require `source`: a mount path
  (`/Content/...`) or a path relative to `project_root`
- a job whose `source` lies in an output folder (a dot-named folder below the
  project root) reads that derived file and names the authored source it
  derives from in `derived_from`, a mount path in a `Source` mount; that
  authored source is recorded as the producer. `derived_from` on a source
  outside output folders fails with `import.derived_from_not_derived`, one that
  is not a mount path in a `Source` mount with `import.derived_from_invalid`
- a job that cooks one asset (`material-descriptor`, `geometry-descriptor`,
  `scene-descriptor`, `script`, `texture`, `texture-descriptor`,
  `physics-material-descriptor`, `collision-shape-descriptor`) requires
  `virtual_path`, the asset's identity. Its descriptor is written at that path
  below the mount (`/Content/Materials/Wood.omat` lives at
  `Materials/Wood.omat`), and its key derives from it. A texture descriptor
  states no `virtual_path`; one that does fails schema validation.
- a reference in an authored file is an absolute virtual path in another mount
  (`/Engine/Materials/Default.omat`) or text relative to the folder of the
  referring file (`Brick.otex`, `../Textures/Brick.otex`), resolved against the
  job's identity. Text the grammar refuses fails with
  `import.reference_invalid`, whose message names the grammar's code:
  `vpath.reference_self_absolute` for absolute text naming the file's own
  mount, `vpath.reference_outside_mount` for a climb above the mount. A path
  in no mount fails with `import.reference_unmounted`. A texture descriptor's `source` and
  `sources[].file` use the same grammar and name files on disk.
- an `input` job requires `virtual_path`, the input source's identity
  (`/Content/Input/Player.input`); each action and mapping context is named
  under it (`/Content/Input/Player.input/Jump.oiact`, written at
  `Input/Player.input/Jump.oiact`).
- a model job (`gltf`, `fbx`) requires `virtual_path`, the model's identity: the
  virtual path of its source file, ending in its extension
  (`/Content/Models/Ship.glb`). Each material, geometry, texture and scene is named
  flat under it (`/Content/Models/Ship.glb/Paint.omat`, written at
  `Models/Ship.glb/Paint.omat`); the scene takes the model's stem
  (`Ship.oscene`).
- the single-file `texture`, `script` and `input` commands require
  `--virtual-path`, as do `gltf` and `fbx` without `--record`
- `texture-descriptor` jobs:
  - use `source` as descriptor JSON path; the identity comes from the job's
    `virtual_path` only
  - support texture tuning keys (`intent`, `mip_policy`, `output_format`, etc.)
    as manifest-level defaults/job overrides
  - reject non-texture keys (for example scene/script-specific keys)
- `material-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output` and `name`
  - descriptor `textures.*.virtual_path` is a reference to a `.otex` asset that
    resolves to a mounted cooked root
- `geometry-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output` and `name`
- `buffer-container` jobs:
  - use `source` as the container JSON path and take no `virtual_path`: the
    container produces several buffers
  - each buffer's `virtual_path` names its product relative to the container
    (`"virtual_path": "Truck_p0_vb.obuf"` in `Truck.buffers.json`), resolved
    as any reference
- `scene-descriptor` jobs:
  - use `source` as descriptor JSON path
  - accept `output` and `name`
  - resolve geometry/material/script/input/physics references, written as
    references to the job's identity, from mounted cooked roots
- `input` jobs require:
  - `id`
  - `source`
  - `virtual_path`
  - optional `depends_on` (array of job ids): the jobs producing the actions of
    other sources this source maps
  - optional `output`
  - allowed keys are exactly: `id`, `type`, `source`, `virtual_path`,
    `depends_on`, `output`, `cooked_context_roots`, `source_key`
- the cooked root's layout is fixed: every descriptor is placed by its
  identity, and the resource tables and data live under `Resources/` (physics
  under `Physics/Resources/`). A manifest has no `layout` field.
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
  "project_root": "..",
  "mounts": [{ "name": "Content", "path": "Content" }],
  "output": "../.cooked/Content",
  "jobs": [
    {
      "type": "gltf",
      "source": "/Content/backpack.glb",
      "virtual_path": "/Content/backpack.glb",
      "name": "backpack_scene"
    },
    {
      "type": "script",
      "source": "/Content/backpack_rotate.lua",
      "virtual_path": "/Content/Scripts/backpack_rotate.oscript",
      "compile": true,
      "script_storage": "embedded",
      "name": "backpack_rotate"
    },
    {
      "type": "script-sidecar",
      "target_scene_virtual_path": "/Content/backpack.glb/backpack.oscene",
      "bindings": [
        {
          "node_index": 0,
          "slot_id": "main",
          "script_virtual_path": "/Content/Scripts/backpack_rotate.oscript",
          "execution_order": 0,
          "params": [{ "key": "speed", "type": "float", "value": 1.0 }]
        }
      ]
    },
    {
      "type": "physics-sidecar",
      "target_scene_virtual_path": "/Content/backpack.glb/backpack.oscene",
      "bindings": {
        "rigid_bodies": [
          {
            "node_index": 0,
            "shape_ref": "/Content/Physics/Shapes/backpack_body.ocshape",
            "material_ref": "../Physics/Materials/default.opmat",
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
`4`. Each job lists:

- `importer`: the importer that cooks the job and its revision, as
  `{name, revision}`, the same as `versions` reports. A job no importer handles
  has none.
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
