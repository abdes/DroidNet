# Per-slot texture sampling: plan

Status: proposal

Last updated: 2026-11-05

Every texture a material uses keeps the UV set, UV transform, wrap and filter
its author gave it. This plan implements
[#113](https://github.com/abdes/DroidNet/issues/113) and
[#107](https://github.com/abdes/DroidNet/issues/107) together, because both add
the same per-slot state to the same material record and change the same shader
lines.

## Contents

- [Status](#status)
- [What the user gets](#what-the-user-gets)
- [What exists today](#what-exists-today)
- [Decisions](#decisions)
- [Formats](#formats)
- [Slices](#slices)
- [Rules for every slice](#rules-for-every-slice)
- [What goes away](#what-goes-away)
- [Test impact](#test-impact)
- [Open question](#open-question)

## Status

| Slice | Content                                  | Status      |
| ----- | ---------------------------------------- | ----------- |
| S1    | A second UV set through geometry         | Done        |
| S2    | Renderer samples each slot on its own    | Done        |
| S3    | Cooked format, descriptors and importers | Done        |
| S4    | Material Editor                          | Not started |

## What the user gets

- **An artist imports a model whose band uses a second UV set**, such as the
  Chronograph Watch. The carbon fibre lands where it was authored, with no
  warning.
- **A material tiles its detail normal map but not its base colour.** Each
  texture keeps its own transform, and the normal map lights correctly when it
  is rotated or mirrored.
- **A label is clamped, a floor mirrors, pixel art stays sharp.** Each keeps its
  wrap and filter, including when two materials use one image differently: the
  image is not duplicated.
- **A technical artist authors a material by hand or in the Material Editor.**
  The same fields exist in the descriptor and in the editor, and existing
  materials render as before: repeat, trilinear, first UV set, no transform.

## What exists today

Paths are under `src/Oxygen`. These correct what the issues say where the code
has moved on.

- **Transforms are read, but only one is kept.** `gltf/GltfAdapter.cpp:1089`
  reads `texCoord` and `KHR_texture_transform` into each binding, and the
  descriptor reader does the same (`MaterialSource.cpp:735`).
  `BuildMaterialUvTransformDesc` (`MaterialPipeline.cpp:178`) then keeps those
  of the first assigned slot for the whole material. So a uniformly transformed
  material already works; #113's "not read" is out of date.
- **FBX reads neither.** It sets `uv_set = 0` and no transform
  (`fbx/FbxAdapter.cpp:2768`), and takes only `mesh.vertex_uv` (`:733`).
- **The shader ignores `uv_set`.** `ApplyMaterialUv` holds a TODO
  (`MaterialShadingConstants.hlsli:63`). The glTF adapter works around it by
  storing in the only stream the set the material's first texture reads
  (`GltfAdapter.cpp:2055`), so a material that reads set 1 everywhere renders
  right today.
- **Samplers are not read.** Only `DroppedSourceFeatures.cpp:205` looks at them,
  to warn. Material passes hard-code `K_SAMPLER_LINEAR_WRAP`, which is trilinear
  without anisotropy (`Direct3D12/Graphics.cpp:374`).
- **Vortex samples 6 of the 12 slots:** base colour, normal, metallic,
  roughness, ambient occlusion, emissive. Masked passes sample base colour alone
  (`MaskedAlphaTest.hlsli:82`).
- **Nothing calls `MaterialBinder::OverrideUvTransform`.**
- **`Graphics/Common/Sampler.h` has no implementation in use:** `SamplerDesc`
  appears nowhere else, and the D3D12 backend fills `D3D12_SAMPLER_DESC` itself.
- **The editor knows none of this:** `Oxygen.Editor.Materials` and
  `Oxygen.Editor.MaterialEditor` have no UV or sampler field.
  `MaterialSourceWriter` keeps a binding's other fields when it changes its
  path (`:121`).

## Decisions

Each names what was rejected. Engine parity is glTF's model (per texture
reference), three.js (the same) and Godot (`UV2`), with UE and Unity going
further through graphs and per-texture settings.

| Question             | Decision                                                                                                                                                                                                                                              | Rejected, and why                                                                                                                      |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| How many UV sets     | Two. glTF says clients should support at least two, and no content in the tree needs more                                                                                                                                                             | More than two: no consumer                                                                                                             |
| Where set 1 lives    | In `data::Vertex`, after `color`: 72 to 80 bytes. Every pass reads one vertex struct, and the field at the end keeps positional initializers                                                                                                          | An optional stream per mesh: every vertex pass then branches on it, to save 8 bytes per vertex                                         |
| A mesh without set 1 | `texcoord1` equals `texcoord`, in adapters and procedural generators, so a binding to set 1 falls back to set 0 as it does today                                                                                                                      | Zeros: the texture samples one texel                                                                                                   |
| A binding past set 1 | The schema refuses it. glTF sets past 1 keep `mesh.uv_sets_dropped`, and that binding uses set 0 with `material.uv_dropped`                                                                                                                           | Remapping set 2 into slot 1: changes what other bindings read                                                                          |
| Where sampling lives | Per slot, for all 12 slots in the cooked record, in array form                                                                                                                                                                                        | Per texture: glTF's state belongs to the reference, and one image would need duplicating. Per material: the case both issues exist for |
| What the GPU gets    | A record for each of the 6 sampled slots. A slot joins when Vortex starts sampling it                                                                                                                                                                 | All 12 now: 192 bytes nothing reads                                                                                                    |
| Filter state         | Mag, min and mip separately, as glTF authors them, with mip `none` as nearest with max LOD 0.25, because at max LOD 0 a minified pixel takes the mag filter                                                                                           | The issue's point, linear, trilinear: mag nearest with mipmapped min is common for pixel art and would still warn                      |
| How samplers exist   | A fixed table of all 108 combinations (3 x 3 address, 2 mag, 2 min, 3 mip), created at startup. An index is computed, never looked up                                                                                                                 | Created on demand: a cache and lifetime for objects that cost one descriptor each, in a domain of 256                                  |
| Shader UVs           | One `float4 uv` interpolator holding both sets, replacing `float2 uv` in every vertex pass                                                                                                                                                            | Resolving the set in the vertex shader: one vertex has several slots                                                                   |
| GPU transform form   | `basis_u`, `basis_v`, `offset`, already multiplied by the binder                                                                                                                                                                                      | Scale, rotation, offset: sin and cos per pixel per slot                                                                                |
| Packed ORM occlusion | Always sampled on its own                                                                                                                                                                                                                             | Reusing the ORM sample when equal: it must then compare index, set, transform and sampler, to save one fetch                           |
| Rotated normal maps  | The sample's xy goes back through the slot's rotation and mirror, not its tiling. The glTF extension is silent; without it, lighting is wrong                                                                                                         | Ignoring it, as Khronos' viewer does with authored tangents                                                                            |
| Tangents             | MikkTSpace runs on the UV set the surface's normal texture reads, as glTF prescribes. `TriangleRange` carries that set from the adapter                                                                                                               | Always set 0: normal maps on set 1 light wrongly                                                                                       |
| FBX set names        | `ufbx_texture.uv_set` is a name. A material's binding is 0 if the name is the first set of the first mesh using it, else 1. Slot 1 of a mesh is the other set its materials name, else its second. A mesh that disagrees warns `mesh.uv_set_conflict` | Storing names in materials: nothing else resolves them                                                                                 |
| Anisotropy           | None, as today                                                                                                                                                                                                                                        | Adding it here: it changes every render and no issue asks for it                                                                       |

The transform keeps the glTF convention the shader already has: scale, then
counter-clockwise rotation, then offset, with the UV origin at the image's upper
left. The extension's matrix expands to
`x' = c*x - s*y, y' = s*x + c*y`, which is `ApplyMaterialUv` today. FBX flips V
at the vertex (`FbxAdapter.cpp:734`), so its `uv_to_texture` matrix is conjugated
by that flip, then decomposed into scale, rotation and offset. A shear that does
not decompose warns `material.uv_dropped` and keeps the nearest transform.

`MaterialShadingConstants` grows from 112 to 272 bytes. It lives in a structured
buffer, so its `kRootConstantsMaxSize` assertion goes, and with it the constant,
which nothing else uses.

## Formats

**Core** gains `Core/Types/TextureSampling.h`, the one owner of the enums and of
the table index, used by Data, Graphics and Vortex:

```cpp
enum class TextureAddress : uint8_t { kRepeat, kMirror, kClamp };
enum class TextureFilter : uint8_t { kNearest, kLinear };
enum class TextureMipFilter : uint8_t { kNone, kNearest, kLinear };
```

**Cooked material** (`Data/PakFormat_render.h`). `uv_scale`, `uv_offset`,
`uv_rotation_radians` and `uv_set` leave `MaterialAssetDesc`, 21 bytes, and
`TextureSlotSampling texture_sampling[12]` takes their place, 312 bytes. The
record grows from 306 to 597 bytes, indexed by a new
`data::MaterialTextureSlot`:

```cpp
struct TextureSlotSampling { // 26 bytes, packed
  float uv_scale[2] = { 1.0F, 1.0F };
  float uv_offset[2] = { 0.0F, 0.0F };
  float uv_rotation_radians = 0.0F;
  uint8_t uv_set = 0;
  uint8_t address_u = 0; // TextureAddress::kRepeat
  uint8_t address_v = 0;
  uint8_t mag_filter = 1; // TextureFilter::kLinear
  uint8_t min_filter = 1;
  uint8_t mip_filter = 2; // TextureMipFilter::kLinear
};
```

`Data/MaterialDescriptor.cpp` refuses a set past 1, an enum out of range and a
non-finite transform.

**GPU record**, one per sampled slot, 32 bytes, mirrored in
`Vortex/Types/MaterialShadingConstants.h` and its `.hlsli`. `uv_scale` to the
last `_pad` go, and `_pad2` stays:

```hlsl
struct MaterialSlotSampling {
    float2 uv_basis_u;
    float2 uv_basis_v;
    float2 uv_offset;
    uint   uv_set;
    uint   sampler_index;
};
```

**Descriptor** (`oxygen.material-descriptor.schema.json`). In `texture_binding`,
`uv_set` gets `maximum: 1`, and an optional `sampler` is added. Each field is
optional and defaults to today's behaviour:

```json
"sampler": {
  "address_u": "repeat | mirror | clamp",
  "address_v": "repeat | mirror | clamp",
  "mag_filter": "nearest | linear",
  "min_filter": "nearest | linear",
  "mip_filter": "none | nearest | linear"
}
```

**Bindless ABI** (`Core/Meta/Bindless.yaml`, its schema and `BindlessCodeGen`).
A `static_sampler_tables` entry names a base slot and a count, here slot 4 and
108, and generates `kTextureSamplerTableIndex`. Graphics creates the table in a
loop after the four static samplers, and the repeat, linear, linear, linear entry
matches `linear_wrap`.

## Slices

Each slice is one commit. Its proof is in [the rules](#rules-for-every-slice).

### S1. A second UV set through geometry

The vertex format changes, and a material's single `uv_set` starts to work.

- `Data/Vertex.h`, `AlmostEqual`, `Contracts/Draw/Vertex.hlsli`; geometry format
  version. `Data/MeshBufferRules.cpp` checks both sets for finite values, and
  `GeometryUploader.cpp:96` does too.
- `MeshStreamView::texcoords1`. The glTF adapter reads `TEXCOORD_0` and
  `TEXCOORD_1` into their own streams and drops its "preferred set" choice
  (`GltfAdapter.cpp:2043-2098`). The FBX adapter reads the mesh's two sets with
  the naming rule above and their authored tangents.
- `MeshBuildPipeline`: fill set 1 from set 0 when absent, zero bad values under
  `kMeshInvalidUvs`, drop no vertex for them. `MeshOptimization.cpp:99` must weld
  on set 1, which S1 proves by test. `LodGeneration` gets a `texcoords1`
  attribute, set only when a surface's sets differ, so seams survive
  simplification.
- `TriangleRange` gains the normal's UV set, and `GeometryPipeline_tangents`
  generates on it, in the frame check too.
- Procedural generators and the `Async`, `VortexBasic` and `Physics` examples
  fill set 1.
- `waterbottle_waterbottle_p0_vb.bin`, the only prebuilt vertex buffer in the
  Showcase, is rewritten at stride 80 with set 1 equal to set 0, and its manifest
  updated.
- All vertex passes pass `float4 uv`. `ApplyMaterialUv` selects by `uv_set`,
  which removes its TODO.
- `mesh.uv_sets_dropped` and the FBX drop at `DroppedSourceFeatures.cpp:420` now
  fire past two sets. `mesh.uv_set_conflict` no longer fires for glTF.

Tests: Vertex layout and equality; welding keeps vertices that differ only in
set 1; LOD seam; tangents on set 1; glTF and FBX fixtures with two sets, with one
set, and with a third; the existing `MaterialUvGpu_test` with `uv_set` 1.

### S2. The renderer samples each slot on its own

No render changes: the binder fills all six records from the cooked material's
single transform, with the default sampler.

- Bindless table, its generator and test (`test_static_samplers.py`), and
  creation in `Graphics.cpp`. `Core/Types/TextureSampling.h`.
- `MaterialSlotSampling` in both mirrors. `SampleSlot` in the shader contract
  replaces `ApplyMaterialUv`. Every sample site in `ForwardMaterialEval.hlsli`
  uses it, normal maps take the frame correction, occlusion is always its own
  sample, and `MaskedAlphaTest.hlsli` loses its `linear_sampler` parameter, so
  its callers in every masked pass drop theirs.
- `MaterialBinder` writes the records, and `ShadowCasterDependencies.cpp:133`
  keys on the base colour record. Its material key and
  `RuntimeMotionProducerModule.cpp:89` hash the asset, which holds one transform
  until S3.
- `OverrideUvTransform` is deleted, with the pointer map only it read, and so is
  `kRootConstantsMaxSize` in `Core/Constants.h`.

Tests: `MaterialSlotSamplingGpu_test` runs the production material function on
constants it writes itself, through `MaterialSurfaceProbe.hlsl`, a compute shader
whose 2x2 thread groups give texture sampling its derivatives. One texture per
slot, each on its own set, transform and sampler. Clamp does not wrap, mirror
mirrors, nearest stays sharp, no-mip does not minify. A rotated and a mirrored
normal map match their unrotated reference. `MaterialUvGpu_test` checks the
record's basis against the independent reference.

### S3. Cooked format, descriptors and importers

Authored state reaches the renderer. This closes issue #113 and the engine's
share of issue #107.

- `PakFormat_render.h`, `MaterialLoader.h`, `MaterialAsset`, `MaterialDescriptor`
  validation and PakDump. The material asset version moves.
- Schema; `MaterialSource` reads `sampler`. `IsOrmPacked` and `SameUvs` compare
  the whole record, and `ReplaceNonFiniteInputs` already covers transforms.
- `BuildMaterialUvTransformDesc` goes, and `MaterialPipeline` writes each slot.
  The binder reads each slot, which ends S2's shared fill.
- glTF: `cgltf_sampler`, mapping undefined filters to linear, and `min_filter`
  to min and mip. FBX: `wrap_u`, `wrap_v`, `uv_set` and the converted transform,
  with default filters.
- `ReportDroppedUvs` goes: the schema refuses a set past 1, and
  `RepairModelInputs` moves a model's to set 0 with `material.uv_dropped`. The
  `conflicting` and `custom_sampling` drops in `DroppedSourceFeatures.cpp` go,
  with FBX's drops of wrap and transform.
- Importer revisions and goldens: gltf, fbx and material_descriptor.
- `oxygen.material-descriptor.editor.schema.json` and the managed reader follow,
  as a row in the portable-references plan's S7. The engine does not wait for it.

Tests: schema accept and reject cases with error substrings; `MaterialSource`
and `MaterialPipeline` per-slot cases, including one image under two samplers
staying one texture; a glTF fixture with one material per sampler case and a
per-slot `KHR_texture_transform`; an FBX fixture with clamp, a named second set
and a transform; `DroppedSourceFeatures_test` flipped to no warning; load tests
reading the record through `Support/CookedRecords.h`.

Done when, beyond the rules: the Chronograph Watch imports with no
`mesh.uv_set_conflict`, `mesh.uv_sets_dropped` or `import.source_feature_dropped`
for UVs or sampling, and its band renders where authored. The file exercises set
1, per-slot transforms and clamp. NewSponza and BurgerPiz are not in this tree,
so the owner imports them.

Proof order, because the material record's version moves and readers refuse the
old one, so all cooked content is stale until this runs:

1. Build Release, then run the Data, Content, Cooker, Vortex and SceneSync lanes.
2. Remove the Showcase cooked root, recook it, and pak it. Each warning is traced.
3. Play every scene of `Examples.json` and capture it after exposure settles.
   Differences from S2's captures are traced: a glTF sampler or a per-slot
   transform now takes effect, so some are expected. A filter of no mips is
   honoured as authored.
4. Import the Chronograph Watch and check the warnings above and its band.
5. A clean recook removes the transient Sponza, Bistro and Sun Temple imports, so
   they are imported again and played.
6. The owner checks the renders, then runs tidy, then the slice is committed.

### S4. Material Editor

Closes #107. It needs portable-references S7, which owns the editor's material
formats.

S4 changes no cooked format and no importer, so it needs no Showcase recook. Its
proof is one material authored in the editor with a non-default sampler, cooked
through the editor's import and played. If S4 does touch the engine, the S3 proof
order applies again.

- Each texture slot shows and edits UV set, transform, wrap U, wrap V and the
  three filters, with undo, dirty state, save and reload.
- `MaterialEdits`, `MaterialValidation`, `MaterialDescriptors` and
  `MaterialSourceWriter`, using the existing Inspector controls.
- Tests: round trip of every field, no field written when it holds its default,
  and the view lane. The owner runs the editor tests.

See the [open question](#open-question) before code.

## Rules for every slice

- The owner builds and runs oxytidy. I run filtered Release lanes, with GPU lanes
  at `-j 1`.
- Proof: clean recook of the whole Showcase, pak with no warnings, all scenes
  play, the owner checks renders. S1 and S2 must render as before; any difference
  is traced to its cause.
- Each warning in the logs is traced to its code.
- Docs change with the slice that changes the fact:
  `material_work_pipeline_v2.md`, `geometry_work_pipeline_v2.md`,
  `material-cooking-architecture.md`, `geometry-cooking-architecture.md`,
  `design/editor/lld/content-pipeline.md:875`, and the Vortex shader contracts
  LLD.
- Maintained content recooks; no reader keeps an old form.

## What goes away

| Old form                                                     | Slice |
| ------------------------------------------------------------ | ----- |
| "Preferred UV set" selection in the glTF adapter             | S1    |
| TODO and no-op branch in `ApplyMaterialUv`                   | S1    |
| 72-byte vertex literals in the buffer manifest and fixtures  | S1    |
| `ApplyMaterialUv`, the `linear_sampler` parameters           | S2    |
| `OverrideUvTransform`, `kRootConstantsMaxSize`               | S2    |
| ORM occlusion reuse branch                                   | S2    |
| Material-wide UV fields in the record, asset, loader and GPU | S2/S3 |
| `BuildMaterialUvTransformDesc`                               | S3    |
| `conflicting` and `custom_sampling` drops                    | S3    |

`Graphics/Common/Sampler.h` is not used by this plan and stays untouched.

## Test impact

Counted by search, not yet edited.

| Slice | Tests that change                                                                                                                                                                                                                                                |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S1    | `Vertex_test`, `MeshBufferRules_test`, the stride kit in `GeometryDescriptorImportJobTestSupport.h` and its 72-byte fixtures, `GeometryPipeline_functional_test`, `LodGeneration_test`, `GltfImportFidelity_functional_test`, `ImporterRevision_functional_test` |
| S2    | `MaterialBinder_binding_test`, `MaterialUvGpu_test`, `LightingGpuAbiProbe.hlsl`, `test_static_samplers.py`, new `MaterialSlotSamplingGpu_test` and `TextureSampling_test`                                                                                        |
| S3    | `PakFormatLayout_render_test`, `MaterialLoader_test`, `MaterialPipeline_functional_test` (40 uses), `MaterialSource_test`, `MaterialDescriptorSchema_test`, `DroppedSourceFeatures_test`, `ImporterRevision_functional_test`, every test filling the asset's UVs |
| S4    | `MaterialSourceTests` (14 uses), `Oxygen.Editor.Materials` and `MaterialEditor` suites                                                                                                                                                                           |

## Open question

**How the Material Editor shows eight more fields per texture slot.** This is
the owner's call before S4, from an SVG mockup in the real editor style. My
starting point is a collapsed row under each assigned slot, showing only values
that differ from the default.
