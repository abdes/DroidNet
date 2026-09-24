# Lighting authored-property inventory

This inventory describes retained authoring fields, current production lighting
model 2 semantics and the remaining ingress/mutation validation work.
Read with the [LightingService contract](lighting-service.md),
[GPU ABI contract](lighting-gpu-abi.md) and
[production PBR model](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2).
This inventory maps retained fields and explicitly approved removals; absent
transport/consumers for retained fields are repair work for EX07-C, not additional
feature exclusions. D2 approves removing LP16/LP17; their IDs remain as migration
and obsolete-input rejection obligations.

## Ingress and persistence owners

Engine-relative sources below are authoritative evidence of the current route;
they are not a claim that the route implements the target domains.

| Code | Current owner / route                                                                                                                                                                                                                   | Review result                                                                                                                                                                                                                         |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| N    | `src/Oxygen/Scene/Light/{LightCommon,DirectionalLight,PointLight,SpotLight}.h`, `SceneNode` attach/replace and mutable component access                                                                                                 | Most scalar setters/Common edits accept unchecked values; paired cone setters clamp independently; CSM canonicalization replaces invalid values. Whole-candidate validation/notification is required.                                 |
| S    | `src/Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeLightBindings.cpp`                                                                                                                                                                  | Has common, shadow, local and old directional fields; uses legacy sun/environment authority. Migrate field registry, complete-candidate validation and errors.                                                                        |
| J    | `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`, `Import/Internal/Jobs/SceneDescriptorImportJob.cpp`                                                                                                             | Source schema has weak numeric/enum domains, no explicit atmosphere slot, per-pixel transmittance or disk RGBA scale.                                                                                                                 |
| P    | `src/Oxygen/Data/PakFormat_world.h`, `PakFormatSerioLoaders.h`, `SceneAsset.*`, `src/Oxygen/Content/Loaders/SceneLoader.h`                                                                                                              | Current packed sizes: shadow 13, common 35, directional 92, point 56, spot 64 bytes. These are current disk formats, not the proposed GPU layout.                                                                                     |
| L    | `Examples/DemoShell/Services/SceneLoaderService.cpp`, `ApplyCommonLight` and light construction                                                                                                                                         | Reconstructs native settings from current records, including old directional booleans. Migrate with P; no slot inference from legacy booleans.                                                                                        |
| E    | repo `projects/Oxygen.Editor.World/src/{Components,Serialization}/*Light*`; `Oxygen.Editor.WorldEditor/src/Documents/Commands/DirectionalLightDescriptors.cs`; `Inspector/DirectionalLightViewModel*`                                   | Editor persistence contains common shadow tuning; directional UI/property keys use old environment/sun fields. Local DTOs omit attenuation model. Follow the existing authoring/inspector contract; do not add a general UI redesign. |
| I    | repo `projects/Oxygen.Editor.Runtime/src/Engine/RuntimeAttach*Light.cs`, `NativeRuntimeCommandTransport.cs`; `Oxygen.Editor.Interop/src/{Commands/AttachLightCommand.h,Commands/DirectionalLightPropertyApplier.h,World/OxygenWorld.*}` | Directional attach carries shadow/CSM fields. Local attach omits model and common shadow tuning. Directional property batches can skip invalid fields and accept others. These must become atomic validated requests.                 |
| M    | repo `projects/Oxygen.Managed.Assets/src/Import/Scenes/{LightCommon,DirectionalLight,PointLight,SpotLight}Source.cs` and source writer                                                                                                  | Common source DTO omits shadow settings; directional DTO omits CSM and explicit slot controls. Save alone cannot establish cook/live parity.                                                                                          |
| T    | `Cooker/Import/Internal/{ImportedLightSemantics.h,gltf/GltfAdapter.cpp,fbx/FbxAdapter.cpp,Pipelines/ScenePipeline.cpp}`; `Cooker/Tools/{PakGen,PakDump}`                                                                                | Import adapters, Python packers/tests and dump tooling are format producers/consumers too. Migrate explicit defaults/physical units and strict record layouts together.                                                               |

All retained fields pass J/P/L and native N. S, E/I and M must preserve fields
they expose; absent controls are identified below rather than assumed present.
Retain native-only data through editor snapshot/save/cook without inventing an
inspector control. The C++20 SDK boundary must expose owned values/results and
native identity; no renderer-private header or C++23 transitive dependency.

## Validation and invalidation keys

All numeric inputs are finite. Validate complete candidates before changing the
accepted scene revision; report source identity, field, reason and requested/
allowed values. UI clamping, where specified by the inspector, happens before
the same candidate validation. Source/cook/load/native APIs do not silently
canonicalize invalid candidates. Unknown enums, malformed arrays, NaN/Inf and
derived overflow fail. Persist base values, not compensated light intensity.

Each inventory row names its required tests. Every retained-field row requires: a
non-default valid edit, an invalid edit retaining the entire previous revision,
save/reload and source/cook/PAK/native comparison, plus live mutation while prior
frames are in flight. Defaults alone never qualify a row. Approved removal rows
instead require migrated producers, absent API/wire fields and strict rejection
of obsolete source/packed inputs; they do not acquire replacement consumers.

- **Q**: rebuild eligibility/ordered selection, identity and view associations.
- **V**: update shared evaluation records and affected per-view lists/draws.
- **H**: update shadow requests/maps/caster or receiver products.
- **A**: update atmospheric assignment/transport/disk products; surface direct
  contribution retains the source identity.

All changes advance the owning revision. Frame products remain immutable;
visibility-only, hierarchy-only and parameter-only edits need no later transform
edit to take effect. Deleted/replaced scenes, nodes and views cannot reuse stale
identity or indices. Transform scale never changes authored light range/radius;
position uses world transform and orientation uses normalized world rotation.

## Common and scene fields

Defaults distinguish low-level native construction from authored creation.
`common.*` denotes fields inside J/P common records; all source domains below
are enforced by the current schema and complete-candidate validation.

| ID / field                             | Source, default and domain                                                                                                           | Selection / final consumer                                                                                                                                                            | Invalidation / additional tests                                                                                                                                                   |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| LP01 identity/type                     | Node handle plus generation; one light component per node, valid Point/Spot/Directional type; J/P `node` index, E persistent node ID | CPU source identity and ordered collection; GPU `selection_index`; shadow map association                                                                                             | Q/V/H/A. Attach/replace/detach, node reuse, reorder and scene replacement cannot redirect a light/shadow.                                                                         |
| LP02 node visibility/hierarchy         | Scene flags Local/Inherit; authored root Shown, children Inherit; preserve existing root resolution                                  | Effective visibility AND affects-world selects lights; traversal rejects node without pruning a locally Shown child                                                                   | Q/H/A. Hidden parent/shown child, hidden slot occupant conflict, visibility-only edits. Geometry hidden/cast rules are separate.                                                  |
| LP03 `common.affects_world`            | N/S/J/P/L/E/I/M; default true, bool                                                                                                  | Eligibility before all direct/atmospheric consumers; retain stored values and slot while off                                                                                          | Q/H/A. Toggle off/on without changing lux/color/assignment; zero-light clearing.                                                                                                  |
| LP04 `common.color_rgb`                | N/S/J/P/L/E/I/M; (1,1,1), linear RGB; finite nonnegative native components and finite derived output; inspector clamps [0,1]         | Resolved RGB intensity/illuminance, shared BRDF and assigned atmosphere tint                                                                                                          | V/A. RGB primaries/black/tinted luminance; picker conversion once; reject negative/nonfinite/overflow.                                                                            |
| LP05 `common.exposure_compensation_ev` | N/S/J/P/L/E/I/M; 0 EV; inspector [-10,10]; native finite value with representable resolved intensity                                 | CPU retains EV/base units; evaluation boundary resolves `2^EV` exactly once into lux/candela; atmosphere uses the same effective source                                               | V/A. +/-1 EV, zero intensity, compensation overflow, change camera exposure without changing authored light. Direct lighting and atmosphere share the Core photometry conversion. |
| LP06 `common.mobility`                 | Native enum Realtime/Mixed/Baked; default Realtime. Canonical V0.1 authoring is Realtime, without redundant stored mobility choice   | Native capability validation; no fabricated bake contribution. Remove obsolete source/editor mobility choices in strict migration, preserve native enum API where otherwise supported | Q. Reject unknown values and unavailable authored bake modes. Explicit exclusion EV01-LIGHT-BAKING, not proof of a bake renderer.                                                 |
| LP07 `common.casts_shadows`            | N/S/J/P/L/E/I/M; native false; explicit authored directional creation true under inspector contract                                  | `flags` bit 0; ShadowService request, per-view reference and both direct-light families                                                                                               | H. On/off, unassigned/Primary/Secondary, missing requested map fails. Do not derive from geometry Cast Shadows.                                                                   |
| LP08 `common.shadow.bias`              | N/S/J/P/L/E/I/M. Native 0, inspector [0,10] dimensionless                                                                            | CPU shadow request; existing per-light depth-bias owner, applied once                                                                                                                 | H. Nonzero signed depth convention, no double application, local live/round-trip tuning.                                                                                          |
| LP09 `common.shadow.normal_bias`       | Same routes as LP08; native 0.02 m, finite >=0; packed default also 0.02 m                                                           | CPU shadow request; per-light receiver normal/texel offset                                                                                                                            | H. Nonzero effect independent of raster bias; source creation writes explicit default.                                                                                            |
| LP10 `common.shadow.contact_shadows`   | Same routes as LP08; default false                                                                                                   | `flags` bit 1, Cast Shadows and receiver gates; ShadowService Stage 8 contact depth and shared attenuation                                                                            | H. Fixed 0.25 m/16-sample contract, missing depth failure, no allocation on disabled frames, one multiplication with map visibility.                                              |
| LP11 `common.shadow.resolution_hint`   | Same routes as LP08; Medium=1, enum exactly Low/Medium/High/Ultra (0..3); source and binary loading reject values outside 0..3       | CPU shadow request, selected quality/capability resolution reported to caller                                                                                                         | H. Every enum, reject 4/255, mixed-resolution lights retain individual requests; no incidental maximum-hint override or silent quality reduction.                                 |
| LP12 geometry cast/receive             | Node modes, authored root On/children Inherit, opaque/masked casting; material Blend caster excluded                                 | ScenePrep per-instance bits; caster filtering and forward/GBuffer receiver eligibility                                                                                                | H. Different flags on instances sharing material/mesh, off-screen caster, Receive Off retains illumination/AO/IBL and own casting.                                                |
| LP13 position/orientation              | Node local/world transform, parent hierarchy, IgnoreParentTransform; finite position and usable normalized rotation                  | Local `position_ws`/`emitted_direction_ws`; directional opposite ray as `direction_to_source_ws`                                                                                      | Q/V/H/A. Parented transforms, ignore-parent, translate/rotate/scale, axis/sign fixture and no stale bounds.                                                                       |

## Local-light fields

| ID / field                                                  | Source, default and domain                                                                                                                       | Selection / final consumer                                                                                                                                                        | Invalidation / additional tests                                                                                                                                |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| LP14 `luminous_flux_lm`                                     | N/S/J/P/L/E/I/M/T; point/spot 800 lm, finite >=0 and representable resolved candela                                                              | CPU flux retained; shared physical conversion -> `intensity_rgb_cd`; no per-draw conversion copy                                                                                  | V. Point 4*pi and independently integrated squared-cone solid angle; zero/bright/endpoints; exactly-once receiver cosine.                                      |
| LP15 `range`                                                | N/S/J/P/L/E/I/M/T; 10 m; finite >=0 with representable inverse; zero has zero influence                                                          | Center-based range and quartic fade; point sphere and spot cone bounds use range without source-radius expansion                                                                  | V/H. Zero clearing/reactivation, near/at/beyond range, rejection of invalid values, projection and shadow coverage.                                            |
| LP16 `attenuation_model`                                    | Removed from N/S/J/P/L/T under D2                                                                                                                | Remove enum, API, script binding, source/packed field and all producer/consumer plumbing; no GPU selector                                                                         | Reject the obsolete key including former value 0; reject old packed layouts; migrate importers/assets/fixtures and verify no compatibility aliases.            |
| LP17 `decay_exponent`                                       | Removed from N/S/J/P/L/E/I/M under D2                                                                                                            | Remove local exponent and accessors across native/script/editor/source/packed/tooling; no evaluator or GPU member                                                                 | Reject obsolete key including former value 2; migrate serializers/commands/fixtures. Keep unrelated directional CSM distribution_exponent.                     |
| LP18 `source_radius`                                        | N/S/J/P/L/E/I/M; 0 m, finite >=0 with representable derived evaluation                                                                           | Analytic source-size diffuse horizon and specular highlight response; zero selects the punctual response                                                                          | V. Source-size edits update shading; range/cone support and ordinary-spot shadow projection are unchanged. Compare images and numerical-reference differences. |
| LP19 `inner_cone_angle_radians`, `outer_cone_angle_radians` | Spot N/S/J/P/L/E/I/M/T; 0.4/0.6 rad half-angles; soft 0<=inner<outer<=pi/2, hard 0<inner=outer<pi/2; GPU cosine parameters must be representable | CPU retains radians and normalizes flux with both angles; GPU receives outer cosine and inverse cosine width. Ordinary spots use projected shadows; hemispheres use cube coverage | V/H. Atomic cone pairs, invalid/unrepresentable interval rejection, hard step, soft hemisphere, cone bounds, glTF candela conversion and shadow routing.       |

## Directional and atmosphere fields

| ID / field                                                     | Source, default and domain                                                                                                                                                   | Selection / final consumer                                                                                                  | Invalidation / additional tests                                                                                                                  |
| -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| LP20 `intensity_lux`                                           | N/S/J/P/L/E/I/M/T; 100000 lux, finite >=0 with representable compensation                                                                                                    | CPU retains lux; directional array effective `illuminance_rgb_lux`; shared BRDF and assigned atmosphere source              | V/A. Unassigned, each slot, both slots plus fill; independent sum, permutation, tint and per-light EV.                                           |
| LP21 `AtmosphereLightSlot`                                     | Native None/Primary/Secondary; authored None. Missing J/P and current E/I/M/S path must migrate                                                                              | CPU source identity plus explicit slot; GPU None=invalid, Primary=0, Secondary=1                                            | Q/A/H. Uniqueness across stored inactive components; rejected conflict names occupant, lone Secondary retains slot, no fallback sun.             |
| LP22 `AngularSizeRadians`                                      | N/S/J/P/L/E/I/M; native 0; authored 0.00935 rad full diameter. Finite nonnegative with valid atmosphere solid-angle calculation                                              | Atmosphere disk geometry/luminance only; not directional surface record radius                                              | A. Full versus half angle, zero/positive disk behavior, role None inactivity; no claimed PCSS/GGX broadening (EV01-LIGHT-FINITE-SOURCE).         |
| LP23 `UsePerPixelAtmosphereTransmittance`                      | Native false; missing explicit J/P and editor/script transport                                                                                                               | CPU authority flag -> GPU mode bit; common atmosphere directional helper in each surface family                             | V/A. On/off with controlled altitude/path and each slot, no double ground+per-pixel attenuation.                                                 |
| LP24 `AtmosphereDiskLuminanceScale`                            | Native current Vec4 defaults (1,1,1,1); retained RGB is finite >=0 with checked radiance. Fourth lane has no defined consumer and is removed, not assigned opacity semantics | RGB atmosphere-model/disk publication; new source/packed/native/script/editor transport preserves RGB. Direct lux unchanged | A. Non-default RGB scale, both slots, save/cook/load/live mutation, radiance validation; no unused alpha lane or invented alpha control.         |
| LP25 old `environment_contribution`, `is_sun_light`, SunNodeId | Removed from N/S/J/P/L/E/I/M; explicit per-light atmosphere slot is the sole authority                                                                                       | Remove with current-format migration of producers/consumers/examples/tests, not a precedence rule alongside slots           | Q/A. Old source keys/packed record sizes rejected; explicit requested demo inference/injection uses ordinary slot assignment and scene lifetime. |

## Directional shadow fields

All are native N, script S, J/P/L; editor E/I carries directional tuning while
managed source M omits it. Preserve inactive stored values. The common shadow
fields LP07-LP11 apply to every directional independently, not atmosphere slot 0.

| ID / field                       | Default/domain                                                                              | Selection / consumer                                                  | Invalidation / additional tests                                                                |
| -------------------------------- | ------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| LP26 `cascade_count`             | 4; integer [1,4]                                                                            | Per-light CPU CSM request -> indexed cascade range                    | H. Every count, zero/5 rejected, both shadowed atmosphere lights plus independent fill policy. |
| LP27 `split_mode`                | Generated=0, ManualDistances=1; reject all others                                           | Per-light CSM setup branch                                            | H. Mode switch preserves inactive values and updates actual matrices.                          |
| LP28 `max_shadow_distance`       | 160 m; finite >0                                                                            | Per-light coverage and fade extent                                    | H. Camera movement and near/far coverage; reject invalid value rather than defaulting.         |
| LP29 `cascade_distances[4]`      | [8,24,64,160] m; finite retained entries; active distances positive and strictly increasing | Manual CSM split setup; stored inactive values retained and validated | H. Nonuniform array, pair ordering, inactive preservation, multiple identities.                |
| LP30 `distribution_exponent`     | 3; finite >=1                                                                               | Generated CSM split setup                                             | H. Non-default split positions, mode switch, invalid exponent.                                 |
| LP31 `transition_fraction`       | 0.1; [0,1]                                                                                  | Per-light cascade blending in matching shadow sampler                 | H. 0, interior, 1, crossing a boundary; field cannot end at CPU storage only.                  |
| LP32 `distance_fadeout_fraction` | 0.1; [0,1]                                                                                  | Per-light final-distance fade in matching sampler                     | H. Endpoints/interior, moving receiver/camera, both rendering families.                        |

## Owning test and cutover map

### Canonical ingress and packed scene v7

Use one validated whole-candidate scene operation for attach/replace and live
light edits. Preserve native unique ownership and the existing SceneNode light
family; add owned public parameter/result values where needed. Public live
mutable property aliases cannot bypass validation or mutation notification.
Detached construction is validated again when committed into a scene, including
stored atmosphere-slot conflicts. A failed batch changes no property, identity,
revision, undo/redo state or accepted scene. Keep these descriptors/results
C++20-compatible and renderer-private headers out of the SDK boundary.

The current source and packed format is **scene version 7**, with the existing scene descriptor/header envelope and
component table routing. Reject version 6, old record sizes and removed source
keys; recook/repack all affected fixtures and dependent sidecars instead of
adding a runtime compatibility reader. One current version is authoritative in
schemas, native loaders/writers, Python tools and managed source generation.

Packed records remain little-endian with byte packing and checked field reads;
these are disk records, never GPU buffers or interop public structures. Boolean
words/bytes accept only 0/1; enums accept only declared values. Offsets below
are relative to the record start.

| Record / bytes                   | Fields at byte offsets                                                                                                                                                                                                                                                                                                                                                                                        |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `LightShadowSettingsRecord` / 13 | 0 bias:f32; 4 normal_bias:f32; 8 contact_shadows:u32; 12 resolution_hint:u8                                                                                                                                                                                                                                                                                                                                   |
| `LightCommonRecord` / 34         | 0 affects_world:u32; 4 color_rgb:f32[3]; 16 casts_shadows:u8; 17 shadow:13 bytes; 30 exposure_compensation_ev:f32                                                                                                                                                                                                                                                                                             |
| `PointLightRecord` / 50          | 0 node_index:u32; 4 common:34 bytes; 38 range:f32; 42 source_radius:f32; 46 luminous_flux_lm:f32                                                                                                                                                                                                                                                                                                              |
| `SpotLightRecord` / 58           | 0 node_index:u32; 4 common:34 bytes; 38 range:f32; 42 inner_cone_angle_radians:f32; 46 outer_cone_angle_radians:f32; 50 source_radius:f32; 54 luminous_flux_lm:f32                                                                                                                                                                                                                                            |
| `DirectionalLightRecord` / 97    | 0 node_index:u32; 4 common:34 bytes; 38 angular_size_radians:f32; 42 atmosphere_light_slot:u8; 43 use_per_pixel_atmosphere_transmittance:u8; 44 atmosphere_disk_luminance_scale_rgb:f32[3]; 56 cascade_count:u32; 60 cascade_distances:f32[4]; 76 distribution_exponent:f32; 80 split_mode:u8; 81 max_shadow_distance:f32; 85 transition_fraction:f32; 89 distance_fadeout_fraction:f32; 93 intensity_lux:f32 |

Scene slot encoding is None=0, Primary=1, Secondary=2; conversion to GPU slot
encoding is explicit. Shadow resolution is 0..3 and split mode 0..1. Native/
packed common defaults retain affects-world=true, white, Cast Shadows=false,
bias=0, normal_bias=0.02 m, contact=false, resolution=Medium and EV=0. Editor
creation writes its explicitly authored Cast Shadows=On rather than relying on
native defaults. Existing importer policy stays explicit. Canonical V0.1 source
has no mobility selector; its runtime mode is Realtime. Native bake capabilities
remain a separate capability boundary, not new EX07 authored controls.

Directional source keys map directly to the fields at offsets 42, 43 and 44.
CSM field names are retained; `environment_contribution` and `is_sun_light` are
rejected. Disk scale is **RGB luminance scale** in source, native, editor and
shader contracts. The former fourth lane had no opacity consumer and is removed.

Zero local range remains valid with **zero influence**, following the current
source schema and the physical rule that contribution is zero at/beyond range.
Do not turn zero into a 1 mm range. Its inverse range is encoded as zero, no
grid references/deferred draw or shadow maps are required, and later activation
rebuilds the relevant products. Exact zero flux/tint similarly needs no shadow
map while preserving authored settings and identity. This is not a threshold
for discarding dim nonzero contributions.

Directional full disk diameter is finite in [0,pi], as documented by the native
component. Zero disables the analytic disk, retaining direct/atmospheric light;
it cannot manufacture a minimum disk. RGB disk scale is finite nonnegative with
checked resulting radiance; it never changes the underlying direct-light lux.
The existing atmosphere-only angular-size exclusion remains.

Native cooking/loading, PakGen, DemoShell, managed source generation and live
Interop use this layout. LP16/LP17 are removed fields, not reserved bytes.

### Atomic authoring and source assignment

`SceneNode::EditLight<T>` validates a detached stack candidate, then copies only
its properties into the existing component. Failed edits preserve component
identity, values and mutation notifications. Accepted edits use the existing
Scene observer collection. Ordinary value edits allocate no replacement component
and do not rescan slot ownership; a changed assignment checks all stored lights,
including hidden and inactive owners, and names any conflicting node.

Script `light_update(table)`, editor command validation and Interop property batches
follow the same whole-candidate rule. The environment Primary picker edits the
selected light's slot. It stores no parallel scene sun identifier. A conflicting
assignment is rejected without changing another light, dirty state or undo history.
Visibility changes invalidate directional resolution; hiding a parent does not
prune a child explicitly marked Shown. `Core/Lighting/LightPhotometry` owns shared
RGB tint, EV, spot cone and physical-unit conversion for direct and atmospheric
lighting, so authored exposure compensation is applied once.

### Imported local-light range

The glTF punctual-light extension has no standard shadow-casting property.
Oxygen defaults imported lights to casting shadows. Supported
`extras.oxygen.casts_shadows` overrides are honored, with node overrides taking
precedence over light overrides. Missing overrides do not disable shadows;
source node names do not change light types or shadow eligibility.

The glTF adapter preserves explicit positive source ranges. When the source omits
range, the importer uses a configurable **4,096 m** finite fallback, approved on
2026-09-24. `ImportOptions::gltf_omitted_light_range_m`, the manifest field of the
same name and ImportTool's `gltf --omitted-light-range` select this value. The
fallback is in Oxygen meters; explicit source ranges follow import unit conversion.
Neither node scale, source intensity nor camera exposure changes the chosen range.
Zero, negative, nonfinite and unrepresentable explicit ranges are rejected.

This is an explicit approximation of glTF's unbounded inverse-square influence.
A large bounded radius retains distant contributions while keeping finite bounds
and projections in the existing renderer. It can increase cluster overlap and
shadow coverage, so imported lights remain editable after conversion. Godot uses
a 4,096-unit bound; Unity glTFast uses a 100,000-unit fallback. An intensity-based
0.01-lux cutoff was rejected as the default: exposure can make the omitted energy
visible, and that threshold is not an established import convention. Silently
using the native 10 m creation default is also rejected. This policy is general
and contains no scene-name or Sponza-specific condition.

References: [glTF range semantics](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md#range-property),
[Godot conversion](https://github.com/godotengine/godot/blob/master/modules/gltf/extensions/gltf_light.cpp),
[Unity glTFast conversion](https://github.com/Unity-Technologies/com.unity.cloud.gltfast/blob/main/Packages/com.unity.cloud.gltfast/Runtime/Scripts/LightPunctualExtension.cs).
Recooked content is a new content baseline, separate from the original renderer
performance comparison.

The source-to-cooked check on Sponza's `HDRI_SKY` confirms a source point light
at 200 cd with no range or shadow override. The cooked node remains a point at
2513.27417 lm (200 cd), with the expected axis conversion; shadowing follows the
importer's explicit default. The original 10 m cutoff came from the range defect; the corrected v7 content
uses the approved 4,096 m fallback. There
is no basis for converting this named node into an environment light. The bounded
check, source declarations and file hashes are in
[`hdri-sky-source-cooked-check.json`](../../../out/build-tracy-ninja/analysis/vortex/exposure-lightbench/ex07c/hdri-sky-source-cooked-check.json).

The corrected-content field/identity check is in
[`sponza-v7-content-check.json`](../../../out/analysis/ex07c-completion/sponza-v7-content-check.json).

### Suite ownership

| Suite / owner                                                                                    | Required LP coverage                                                                                                                                                   |
| ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Scene/Test` light/component/resolver tests                                                      | LP01-LP32 candidate validation as applicable; source identity, inactive slot conflicts, visibility/hierarchy and mutation notification.                                |
| `Scripting/Test` scene bindings                                                                  | Every exposed row: same rejection/atomic revision as native; replace obsolete sun fields with explicit assignment.                                                     |
| `Cooker/Test/Import/SceneDescriptor{JsonSchema,ImportJob}_test.cpp`, import adapter tests        | Complete non-default J inputs; schema-enforceable ranges/enums; semantic cross-field/derived validation; old format rejection.                                         |
| `Content/Test/AssetLoader_scene_test.cpp`, Data serialization tests, PakGen tests                | Exact new record sizes/fields; malformed binary enums/values; no reinterpretation of old packed light records.                                                         |
| `Examples/DemoShell/Test/SceneLoaderService_phase4_test.cpp` and light/environment service tests | Reconstructed LP fields versus cooked records; existing explicit demo convenience semantics; no implicit production sun.                                               |
| Editor World/Runtime/Interop/Managed.Assets owning tests                                         | Non-default E save/load, M source/cook, I live values, atomic rejection, undo/redo/dirty state and C++20 public boundary. Use prescribed MSBuild, not dotnet.          |
| Vortex LightingService/SceneRenderer/Shadows and native lighting GPU tests                       | Every retained active field changes its declared product; independent photometry/material tests, matching identities, both families and multiview/in-flight mutations. |

GPU values and actual images are required for rendered effects; DTO equality
alone cannot close them. Keep the known explicit exclusions (baking, directional
finite-source shading, Blend casters, authored sky-only/hidden-shadow modes,
more than two atmospheric contributors) distinct from these repairs. EX07-B
qualifies measurement; EX07-C supplies these regression/round-trip cases, and
EX07-F confirms final integration without moving missing transport to closeout.
