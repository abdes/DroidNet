# Oxygen Engine - Unified PAK v7 Animation, Deformation, and Physics Specification

**Date:** 2026-02-28
**Status:** Design / Specification

**Target File:** `src/Oxygen/Data/PakFormat.h`
**Container Version:** `v7` (in-place update)

## 1. Architecture & Compliance Guarantees

### 1.1 Capability Targets & Done Criteria

When this specification is implemented completely, the answers form the baseline done criteria:

1. glTF/FBX import + cook + playback with high fidelity and feature parity: **Yes**.
2. High-fidelity animation-coupled physics integration with Jolt/PhysX (for supported features): **Yes**.

Done only when:

1. full v7 schema is implemented and validated.
2. glTF/FBX parity is met in strict mode.
3. Jolt/PhysX animation-physics parity targets are met in strict mode.
4. CI verifies the yes/yes capability target.

### 1.2 Hard ABI & Parity Rules

1. `PakHeader.version` remains `7`.
2. All changes are direct `v7` schema changes (no alternate format path).
3. Persisted ids/enums/flags must be defined in Core Meta `.inc` catalogs.
4. No silent lossy conversion in strict parity mode.
5. Loader/cooker/runtime must enforce deterministic validation contracts.
6. `reserved[]` fields are never ad-hoc: each one must have a single documented purpose (fixed ABI slot or explicit target struct size).
7. Struct layout process is mandatory and two-pass:
   - define semantic fields first (no padding),
   - then add one intentional `reserved[]` block only if required by canonical ABI targets.
   - interstitial/scattered `reserved0/reserved1/...` fields are forbidden.
8. Numeric `sizeof(...)` literals are canonicalized in `PakFormat.h` only; this design document defines field contracts and invariants, not duplicated size constants.

Strict parity mode:

1. any lossy import mapping => error.
2. any requested backend incompatibility => error.
3. no silent downgrade.
4. no implicit promotion of normalized integer tracks without explicit dequant metadata.

### 1.3 Source Fidelity Mapping Requirements

**glTF**

1. skins -> skeleton+skin
2. targets -> morph sets
3. animations -> clips/samplers/channels
4. `weights` channels -> morph-weight channels

**FBX**

1. takes/stacks/layers -> clip metadata
2. blendshape in-betweens -> morph metadata
3. pre/post rotation policies persisted
4. camera/light animation persisted

**Practical Import Coverage Required Support Targets**

1. `KHR_materials_emissive_strength`
2. `KHR_materials_specular` full
3. `KHR_materials_sheen` full
4. `KHR_materials_clearcoat` full
5. `KHR_materials_iridescence`
6. `KHR_materials_anisotropy`
7. `KHR_materials_variants`
8. `EXT_meshopt_compression` decode
9. `KHR_draco_mesh_compression` decode

### 1.4 Persisted Flag/Id Catalog Requirements

Every persisted non-trivial id/bitfield must have a named Core Meta catalog entry and no anonymous semantics:

1. `AnimationResourceDesc.codec_flags`
2. `AnimationClipAssetDesc.root_motion_flags`
3. `AnimationClipAssetDesc.clip_flags`
4. `AnimationChannelRecord.channel_flags`
5. `RetargetChainRecord.settings_flags`
6. `SkinningConfigRecord.flags`
7. `DeformationCacheAssetDesc.flags`
8. `DeformationLodMapRecord.flags`
9. `PhysicsBoneBodyBindingRecord.flags`
10. `PhysicsBoneConstraintBindingRecord.flags`
11. `PhysicsBackendTuningRecord.op_flags`
12. `DeformationPhysicsProfileRecord.collision_mode/self_collision_tier/tether_mode`
13. `PhysicsTuningOverrideAssetDesc.flags`
14. `PhysicsTuningOverrideRecord.backend_mask/op_flags`
15. `AnimationBindingRecord.flags`
16. `RootMotionPolicyRecord.policy_flags`
17. `MaterialAnimationTrackRecord.flags`
18. `MaterialAnimationBindingRecord.flags`
19. `AnimationRenderContractRecord.motion_vector_mode/flags`

Strict rule: unknown bit/id values in persisted content are hard-fail in cooker and loader.

## 2. Container Foundation (`v7::PakFooter`)

### 2.1 Canonical Footer Layout

`v7::PakFooter` is updated to this canonical fixed-size layout (single layout, no alternatives):

```cpp
// Byte offsets (all absolute in struct layout):
//   0: directory_offset                (8)
//   8: directory_size                  (8)
//  16: asset_count                     (8)
//  24: texture_region                  (16)
//  40: buffer_region                   (16)
//  56: audio_region                    (16)
//  72: script_region                   (16)
//  88: physics_region                  (16)
// 104: animation_region                (16)
// 120: deformation_region              (16)
// 136: texture_table                   (16)
// 152: buffer_table                    (16)
// 168: audio_table                     (16)
// 184: script_resource_table           (16)
// 200: physics_resource_table          (16)
// 216: animation_resource_table        (16)
// 232: deformation_resource_table      (16)
// 248: footer_magic                    (8)
// 256: end
//
// Removed from canonical v7 footer: script_slot_table, browse_index_offset,
// browse_index_size, reserved[], pak_crc32.
```

Normative ABI rules:

1. `sizeof(v7::PakFooter)` is exactly `256`.
2. Field order and offsets above are authoritative.
3. Implementation must add `static_assert(offsetof(...))` for every footer field.
4. This layout supersedes all previous internal v7 footer layouts in this project.
5. Loader must hard-fail any footer that does not match this canonical field map exactly (no legacy in-place variants accepted).
6. `PakHeader.version == 7` is the only supported container version. No compatibility branches, fallback loaders, or variant footer maps are allowed inside v7.

### 2.2 Global Compression Contract (`PayloadCompressionDesc`)

`PayloadCompressionDesc` is shared by animation/deformation/physics resources and uses one canonical meaning:

1. `size_bytes` in the owning resource descriptor is the number of bytes persisted in the region and must equal:
   - `uncompressed_size` when `codec == kNone`,
   - `compressed_size` when `codec != kNone`.
2. When `codec == kNone`:
   - `compressed_size == 0`,
   - `uncompressed_size == size_bytes`,
   - `block_size == 0`,
   - `error_metric == 0`.
3. When `codec != kNone`:
   - `compressed_size > 0`,
   - `uncompressed_size > 0`,
   - `compressed_size <= uncompressed_size`,
   - `block_size > 0` for block codecs and `block_size == 0` only for whole-stream codecs.
4. `content_hash` is computed over decoded (uncompressed) bytes.
5. Loader validation is deterministic and strict: any size mismatch between descriptor fields and decode result is a hard-fail.

## 3. Domain: Core Animation & Tracks

### 3.1 Data Payloads

```cpp
enum class AnimationResourceFormat : uint8_t {
  kScalarF32 = 1,
  kVec3F32 = 2,
  kQuatF32 = 3,
  kWeightsF32 = 4,
  kCubicSplineVec3F32 = 5,
  kCubicSplineQuatF32 = 6,
  kCubicSplineWeightsF32 = 7,
  kScalarF16 = 8,
  kVec3F16 = 9,
  kQuatF16 = 10,
  // Normalized integer tracks (required for high-fidelity glTF parity).
  kScalarUNorm8 = 11,
  kScalarUNorm16 = 12,
  kVec3SNorm16 = 13,
  kQuatSNorm16 = 14,
  kMat4x4F32 = 15,
  kVec4F32 = 16,
  kVec2F32 = 17,
};

enum class PayloadCompressionCodec : uint8_t {
  kNone = 0,
  kZstd = 1,
  kOodle = 2,
  kMeshopt = 3,
};

#pragma pack(push, 1)
struct PayloadCompressionDesc {
  PayloadCompressionCodec codec = PayloadCompressionCodec::kNone;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
  uint32_t block_size = 0;
  uint32_t error_metric = 0;
};
#pragma pack(pop)
// ABI: sizeof(PayloadCompressionDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationResourceDesc {
  OffsetT data_offset = 0;
  DataBlobSizeT size_bytes = 0;
  AnimationResourceFormat format = AnimationResourceFormat::kScalarF32;
  uint32_t element_count = 0;
  uint32_t arity = 1;
  uint32_t codec_flags = 0;
  // Required when format is normalized integer: out = (in * scale) + offset.
  float dequant_scale[4] = { 1.0F, 1.0F, 1.0F, 1.0F };
  float dequant_offset[4] = { 0.0F, 0.0F, 0.0F, 0.0F };
  PayloadCompressionDesc compression = {};
  uint64_t content_hash = 0;
};
#pragma pack(pop)
// ABI: sizeof(AnimationResourceDesc) is canonicalized in PakFormat.h with static_assert.
```

### 3.2 Serialization Invariants

Dequantization constraints:

1. `kScalarUNorm8` / `kScalarUNorm16` decode domain must map to `[0, 1]`.
2. `kVec3SNorm16` / `kQuatSNorm16` decode domain must map to `[-1, 1]`.
3. `kQuatSNorm16` results must be renormalized before interpolation; reject quaternions below minimum norm threshold.
4. `dequant_scale` must be finite and non-zero for quantized formats.
5. `kMat4x4F32` is mandatory for `SkinAssetDesc.inverse_bind_resource_index` payloads.
6. `kVec4F32` is required for arbitrary 4D vectors (for example color/vector parameters) and must not use quaternion renormalization rules.
7. Quantized formats must use component-local dequant lanes only up to declared arity; unused lanes are persisted as identity (`scale=1`, `offset=0`).
8. `AnimationResourceDesc.arity` must match `format` exactly (or the logical value arity for cubic-spline packed formats).
9. `element_count` must describe decoded element count; persisted-byte size validation uses the global compression contract in §2.2.

### 3.3 Animation Clips & Metadata

```cpp
enum class AnimationInterpolation : uint8_t {
  kStep = 0,
  kLinear = 1,
  kCubicSpline = 2,
};

enum class AnimationTargetPath : uint8_t {
  kTranslation = 0,
  kRotation = 1,
  kScale = 2,
  kWeights = 3,
  kCameraFovY = 4,
  kCameraNear = 5,
  kCameraFar = 6,
  kLightIntensity = 7,
  kLightColor = 8,
  kLightRange = 9,
  kLightInnerCone = 10,
  kLightOuterCone = 11,
};

#pragma pack(push, 1)
struct AnimationClipAssetDesc {
  AssetHeader header;
  float duration_seconds = 0.0F;
  uint32_t sampler_count = 0;
  uint32_t channel_count = 0;
  OffsetT sampler_table_offset = 0;
  OffsetT channel_table_offset = 0;
  // Optional clip-local links.
  uint32_t material_track_count = 0;
  OffsetT material_track_table_offset = 0; // MaterialAnimationTrackRecord[material_track_count]
  uint32_t event_count = 0;
  OffsetT event_table_offset = 0; // AnimationEventRecord[event_count]
  uint32_t custom_curve_count = 0;
  OffsetT custom_curve_table_offset = 0; // AnimationCustomCurveRecord[custom_curve_count]
  // Clip-local string table for event/curve/track names.
  OffsetT string_table_offset = 0;
  uint32_t string_table_size = 0;
  // Optional root motion extraction channel metadata.
  uint32_t root_motion_flags = 0;
  uint32_t clip_flags = 0;
  uint8_t reserved[53] = {};
};
#pragma pack(pop)
// ABI: sizeof(AnimationClipAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationSamplerRecord {
  ResourceIndexT input_time_resource = kNoResourceIndex;
  ResourceIndexT output_value_resource = kNoResourceIndex;
  AnimationInterpolation interpolation = AnimationInterpolation::kLinear;
  uint32_t keyframe_count = 0;
  uint32_t value_arity = 0;
};
#pragma pack(pop)
// ABI: sizeof(AnimationSamplerRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationChannelRecord {
  uint32_t sampler_index = 0;
  AnimationTargetPath target_path = AnimationTargetPath::kTranslation;
  AssetKey target_node_key = {};
  AssetKey target_morph_set_key = {};
  // For granular single-target blendshape animation.
  uint32_t morph_target_index = 0xFFFFFFFFu; // all targets when invalid
  uint32_t channel_flags = 0; // bit0: targets_single_morph, bit1: root_motion_channel
};
#pragma pack(pop)
// ABI: sizeof(AnimationChannelRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationEventRecord {
  float time_seconds = 0.0F;
  uint32_t event_name_offset = 0;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(AnimationEventRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationCustomCurveRecord {
  uint32_t curve_name_offset = 0;
  uint32_t sampler_index = 0;
  uint32_t value_type = 0; // float/int/bool enum id
};
#pragma pack(pop)
// ABI: sizeof(AnimationCustomCurveRecord) is canonicalized in PakFormat.h with static_assert.

// Flag bit ids are persisted in Core Meta and consumed by loader/runtime exactly:
// - AnimationChannelRecord.channel_flags:
//   bit0 = targets_single_morph, bit1 = root_motion_channel
// - AnimationClipAssetDesc.root_motion_flags:
//   bit0 = allow_translation_extraction, bit1 = allow_rotation_extraction
// - AnimationClipAssetDesc.clip_flags:
//   bit0 = looping, bit1 = additive, bit2 = has_events, bit3 = has_custom_curves
```

Clip table/link invariants:

1. `material_track_count/event_count/custom_curve_count/sampler_count/channel_count` must match table row counts exactly.
2. `param_name_offset`, `event_name_offset`, and `curve_name_offset` must resolve in clip-local `string_table_offset/string_table_size`.
3. `sampler_table_offset/channel_table_offset/material_track_table_offset/event_table_offset/custom_curve_table_offset/string_table_offset` are descriptor-local offsets.
4. `AnimationSamplerRecord.input_time_resource` must resolve to `AnimationResourceFormat::kScalarF32` or `kScalarF16`.
5. Input times must be finite, strictly monotonic increasing, and `keyframe_count >= 1`.
6. For `kStep` and `kLinear`, `output_value_resource.element_count == keyframe_count`.
7. For `kCubicSpline`, `output_value_resource.element_count == keyframe_count * 3` (`in_tangent`, `value`, `out_tangent` triplets).
8. For `AnimationTargetPath::kWeights`, `target_morph_set_key` is required; for every other path `target_node_key` is required.
9. Target-path output-format contract is strict:
   - `kTranslation` / `kScale` -> `kVec3F32` or `kVec3F16`,
   - `kRotation` -> `kQuatF32` or `kQuatF16` or `kQuatSNorm16`,
   - `kWeights` -> `kWeightsF32` unless `targets_single_morph` is set,
   - `kCameraFovY` / `kCameraNear` / `kCameraFar` / `kLightIntensity` / `kLightRange` / `kLightInnerCone` / `kLightOuterCone` -> scalar formats,
   - `kLightColor` -> `kVec3F32` or `kVec4F32`.
10. `value_arity` must match resolved output format arity exactly (including cubic-spline logical value arity).
11. For `AnimationTargetPath::kCameraNear` / `kCameraFar`, sampled values must be finite and `near > 0`, `far > near`.
12. If `channel_flags.targets_single_morph` is set, output format must be `kScalarF32`, `kScalarF16`, `kScalarUNorm8`, or `kScalarUNorm16`; otherwise morph-weight channels must use `kWeightsF32`.
13. If `channel_flags.root_motion_channel` is set, target must resolve to the skeleton root node.
14. `root_motion_flags` constrains legal root-motion channel selection; policy selectors must choose only channels permitted by `root_motion_flags`.
15. `AnimationInterpolation::kStep` is mandatory for `kTextureSlot` and `kKeywordToggle` material-param tracks (no interpolation).
16. Import parity for FBX camera/light animation is satisfied through the camera/light target paths above; importers must emit those channels directly (no custom-curve fallback in strict mode).
17. If `channel_flags.targets_single_morph` is set, `morph_target_index` must be valid for the target morph set.

Clip-local metadata ownership is explicit:

1. Events and custom curves are persisted directly inside `AnimationClipAssetDesc` tables (no implied external sidecar).
2. Additive behavior is persisted through `clip_flags` and runtime binding flags/policies; no implicit metadata lookup is allowed.
3. Retargeting metadata is external (`RetargetProfileAssetDesc`) and is linked explicitly by scene animation bindings.

### 3.4 Retargeting

```cpp
#pragma pack(push, 1)
struct RetargetProfileAssetDesc {
  AssetHeader header;
  AssetKey source_skeleton_key = {};
  AssetKey target_skeleton_key = {};
  uint32_t chain_count = 0;
  OffsetT chain_table_offset = 0;
  OffsetT chain_string_table_offset = 0;
  uint32_t chain_string_table_size = 0;
  uint32_t flags = 0;
  uint8_t reserved[101] = {};
};
#pragma pack(pop)
// ABI: sizeof(RetargetProfileAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct RetargetChainRecord {
  uint32_t chain_name_offset = 0; // chain string table
  AssetKey source_start_joint = {};
  AssetKey source_end_joint = {};
  AssetKey target_start_joint = {};
  AssetKey target_end_joint = {};
  AssetKey source_ik_goal_joint = {};
  AssetKey target_ik_goal_joint = {};
  AssetKey source_pole_joint = {};
  AssetKey target_pole_joint = {};
  float twist_limit_degrees = 180.0F;
  float root_translation_scale = 1.0F;
  uint32_t settings_flags = 0; // translation scaling, root behavior, IK policy
};
#pragma pack(pop)
// ABI: sizeof(RetargetChainRecord) is canonicalized in PakFormat.h with static_assert.
```

Retarget serialization rules:

1. `chain_table_offset` and `chain_string_table_offset` are descriptor-local offsets.
2. Chain names are unique within a profile.
3. Every `chain_name_offset` must resolve within `[chain_string_table_offset, chain_string_table_offset + chain_string_table_size)`.
4. `chain_count` rows at `chain_table_offset` must be fully in-bounds of descriptor payload.
5. `source_skeleton_key` and `target_skeleton_key` must both resolve to `SkeletonAssetDesc`.
6. When `source_skeleton_key == target_skeleton_key`, retarget application is allowed only if profile flags explicitly request same-skeleton remap.

### 3.5 Domain Validation Rules

Mandatory hard-fail diagnostics for Core Animation:

1. sampler/channel index and payload mismatches.
2. retarget chain unresolved joint key.
3. clip-linked table offsets/count mismatch.
4. normalized track format missing dequant parameters.
5. morph channel targets single index but `morph_target_index` out of range.
6. clip string table offset/size invalid for referenced name offsets.
7. `RetargetProfileAssetDesc.chain_string_table_offset/size` must bound every `RetargetChainRecord.chain_name_offset`.
8. quantized tracks must satisfy per-format domain constraints before dequantization.
9. channel target-path contract violations (`kWeights` without morph set key, non-weights without node key) must hard-fail.
10. sampler input times not finite/strictly monotonic.
11. cubic-spline sampler with invalid triplet element count (`keyframe_count * 3`) or incompatible cubic format.
12. sampler `value_arity` mismatch against resolved output format.
13. camera/light target paths with unsupported output formats.
14. `kCameraNear` / `kCameraFar` domain violations (`near <= 0` or `far <= near`).
15. unresolved or duplicated retarget chain names inside one profile.
16. animation resource compression descriptor violations against §2.2 canonical size semantics.
## 4. Domain: Skeletons, Skinning, & Morphs

### 4.1 Skeletons

```cpp
#pragma pack(push, 1)
struct SkeletonAssetDesc {
  AssetHeader header;
  uint32_t joint_count = 0;
  OffsetT joint_table_offset = 0;
  OffsetT joint_name_string_table_offset = 0;
  uint32_t joint_name_string_table_size = 0;
  OffsetT fbx_joint_meta_table_offset = 0; // optional SkeletonFbxJointMetaRecord[joint_count]
  AssetKey root_joint_key = {};
  // Rest bounds for culling/physics broad-phase bootstrap.
  float rest_aabb_min[3] = { 0.0F, 0.0F, 0.0F };
  float rest_aabb_max[3] = { 0.0F, 0.0F, 0.0F };
  float rest_bounding_sphere[4] = { 0.0F, 0.0F, 0.0F, 0.0F }; // xyz + radius
  uint8_t reserved[65] = {};
};
#pragma pack(pop)
// ABI: sizeof(SkeletonAssetDesc) is canonicalized in PakFormat.h with static_assert.

enum class ScaleInheritanceMode : uint8_t {
  kInheritAll = 0,         // Parent TRS full inheritance.
  kInheritNoScale = 1,     // Parent translation+rotation only.
  kInheritUniformScale = 2 // Parent scale forced to uniform scalar.
};

#pragma pack(push, 1)
struct SkeletonFbxJointMetaRecord {
  AssetKey joint_key = {};
  float pre_rotation_quat[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  float post_rotation_quat[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  uint32_t rotation_order = 0; // Core Meta rotation order id
  uint32_t inherit_type = 0;   // FBX inherit type id
};
#pragma pack(pop)
// ABI: sizeof(SkeletonFbxJointMetaRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct SkeletonJointRecord {
  AssetKey joint_key;
  int32_t parent_joint_index = -1;
  uint32_t name_offset = 0;
  ScaleInheritanceMode scale_inheritance_mode = ScaleInheritanceMode::kInheritAll;
  // Local rest/bind transform (required for hierarchy reconstruction).
  float rest_translation[3] = { 0.0F, 0.0F, 0.0F };
  float rest_rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  float rest_scale[3] = { 1.0F, 1.0F, 1.0F };
  float rest_aabb_min[3] = { 0.0F, 0.0F, 0.0F };
  float rest_aabb_max[3] = { 0.0F, 0.0F, 0.0F };
};
#pragma pack(pop)
// ABI: sizeof(SkeletonJointRecord) is canonicalized in PakFormat.h with static_assert.
```

### 4.2 Skinning

```cpp
enum class SkinningModel : uint8_t {
  kLinearBlend = 1,
  kDualQuaternion = 2,
  kHybrid = 3,
};

#pragma pack(push, 1)
struct SkinningConfigRecord {
  SkinningModel model = SkinningModel::kLinearBlend;
  uint8_t max_influences_per_vertex = 4;
  uint16_t max_bones_per_section = 256;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(SkinningConfigRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct SkinAssetDesc {
  AssetHeader header;
  AssetKey skeleton_asset_key = {};
  uint32_t joint_count = 0;
  OffsetT joint_key_table_offset = 0;
  ResourceIndexT inverse_bind_resource_index = kNoResourceIndex;
  // Optional per-vertex blend weights for hybrid LBS/DQS mode.
  ResourceIndexT hybrid_blend_weight_resource_index = kNoResourceIndex;
  SkinningConfigRecord skinning_config = {};
  uint8_t reserved[121] = {};
};
#pragma pack(pop)
// ABI: sizeof(SkinAssetDesc) is canonicalized in PakFormat.h with static_assert.
```

### 4.3 Morphs & Deformation Caches

```cpp
enum class DeformationResourceFormat : uint8_t {
  kMorphDeltaPositionF32x3 = 1,
  kMorphDeltaNormalF32x3 = 2,
  kMorphDeltaTangentF32x3 = 3,
  kMorphSparseIndexU32 = 4,
  kAlembicVertexCache = 5,
  kVertexAnimationTexture = 6,
  kMlDeformerDelta = 7,
  kAttachmentPinMapU32x2 = 8,
};

#pragma pack(push, 1)
struct DeformationResourceDesc {
  OffsetT data_offset = 0;
  DataBlobSizeT size_bytes = 0;
  DeformationResourceFormat format = DeformationResourceFormat::kMorphDeltaPositionF32x3;
  uint32_t element_count = 0;
  uint32_t arity = 3;
  PayloadCompressionDesc compression = {};
  uint64_t content_hash = 0;
};
#pragma pack(pop)
// ABI: sizeof(DeformationResourceDesc) is canonicalized in PakFormat.h with static_assert.

enum class MorphAttributeMask : uint32_t {
  kNone = 0,
  kPosition = (1u << 0),
  kNormal = (1u << 1),
  kTangent = (1u << 2),
};

enum class MorphStorageMode : uint8_t {
  kDense = 0,
  kSparse = 1,
};

#pragma pack(push, 1)
struct MorphTargetRecord {
  uint32_t name_offset = 0;
  MorphAttributeMask attribute_mask = MorphAttributeMask::kNone;
  MorphStorageMode storage_mode = MorphStorageMode::kDense;
  // Offsets into set-level packed resources (bindless-safe, low descriptor usage).
  uint32_t position_delta_offset_bytes = 0;
  uint32_t normal_delta_offset_bytes = 0;
  uint32_t tangent_delta_offset_bytes = 0;
  uint32_t sparse_index_offset_bytes = 0;
  uint32_t sparse_count = 0;
  uint32_t dense_vertex_count = 0;
};
#pragma pack(pop)
// ABI: sizeof(MorphTargetRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct MorphTargetSetAssetDesc {
  AssetHeader header;
  uint32_t target_count = 0;
  uint32_t base_vertex_count = 0;
  OffsetT target_table_offset = 0;
  OffsetT default_weights_offset = 0;
  // Packed shared resources for all targets in this set.
  ResourceIndexT packed_position_delta_resource = kNoResourceIndex;
  ResourceIndexT packed_normal_delta_resource = kNoResourceIndex;
  ResourceIndexT packed_tangent_delta_resource = kNoResourceIndex;
  ResourceIndexT packed_sparse_index_resource = kNoResourceIndex;
  OffsetT target_name_string_table_offset = 0;
  uint32_t target_name_string_table_size = 0;
  uint32_t default_weight_count = 0; // must equal target_count when present
  uint8_t reserved[89] = {};
};
#pragma pack(pop)
// ABI: sizeof(MorphTargetSetAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationCacheAssetDesc {
  AssetHeader header;
  DeformationResourceFormat format = DeformationResourceFormat::kAlembicVertexCache;
  uint32_t frame_count = 0;
  float sample_rate = 0.0F;
  uint32_t vertex_count = 0;
  ResourceIndexT payload_resource_index = kNoResourceIndex;
  ResourceIndexT aux_payload_resource_index = kNoResourceIndex; // optional normals/tangents/secondary stream
  AssetKey source_mesh_asset_key = {};
  uint64_t topology_hash = 0;
  uint32_t vertex_layout_id = 0;
  uint32_t cache_layout_id = 0; // Core Meta cache layout id (AlembicLinear/VAT2D/MlDelta ...)
  uint32_t payload_stride_bytes = 0; // bytes per frame in decoded domain
  uint32_t payload_component_size_bytes = 0; // scalar byte width (1/2/4)
  uint32_t payload_component_count = 0; // per-vertex component count in primary stream
  uint32_t source_lod_index = 0;
  uint32_t flags = 0;
  float rest_aabb_min[3] = { 0.0F, 0.0F, 0.0F };
  float rest_aabb_max[3] = { 0.0F, 0.0F, 0.0F };
  float rest_bounding_sphere[4] = { 0.0F, 0.0F, 0.0F, 0.0F };
  uint8_t reserved[76] = {};
};
#pragma pack(pop)
// ABI: sizeof(DeformationCacheAssetDesc) is canonicalized in PakFormat.h with static_assert.
```

### 4.4 LOD Maps

```cpp
#pragma pack(push, 1)
struct DeformationLodMapRecord {
  uint32_t lod_index = 0;
  AssetKey morph_target_set_key = {};
  AssetKey skin_asset_key = {};
  uint32_t target_remap_offset = 0; // uint32 element offset into target_remap_table
  uint32_t target_remap_count = 0; // uint32 element count
  uint32_t curve_lod_max = 0xFFFFFFFFu;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(DeformationLodMapRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationLodMapAssetDesc {
  AssetHeader header;
  uint32_t record_count = 0;
  OffsetT record_table_offset = 0; // DeformationLodMapRecord[record_count]
  OffsetT target_remap_table_offset = 0; // dense uint32_t remap index payload
  uint32_t target_remap_entry_count = 0; // total uint32 entries in target remap table
  uint8_t reserved[133] = {};
};
#pragma pack(pop)
// ABI: sizeof(DeformationLodMapAssetDesc) is canonicalized in PakFormat.h with static_assert.
```

### 4.5 Domain Validation Rules

Mandatory hard-fail diagnostics for Skeletons, Skinning, & Morphs:

1. skeleton acyclic/root/key consistency failures.
2. skin inverse-bind count mismatch.
3. morph sparse/dense bounds mismatch.
4. LOD remap inconsistency.
5. skeleton rest pose invalid or non-finite.
6. skeleton joint scale inheritance mode invalid.
7. bounds absent or non-finite where required by asset type.
8. `MorphTargetRecord` byte spans must be aligned, non-negative, and fully inside packed morph resources.
9. `ScaleInheritanceMode` must be valid enum and supported by runtime evaluator.
10. non-zero `fbx_joint_meta_table_offset` must provide exactly `joint_count` `SkeletonFbxJointMetaRecord` rows.
11. each `SkeletonFbxJointMetaRecord.joint_key` must resolve uniquely in the owning skeleton.
12. `deformation_lod_map_asset_key` must resolve to `DeformationLodMapAssetDesc` and all referenced rows must be in-bounds.
13. `DeformationCacheAssetDesc` bindings must match mesh topology (`source_mesh_asset_key`, `topology_hash`, `vertex_layout_id`, `source_lod_index`).
14. `DeformationLodMapRecord.target_remap_offset/target_remap_count` spans must be fully in-bounds of `DeformationLodMapAssetDesc.target_remap_entry_count`.
15. `SkinAssetDesc.inverse_bind_resource_index` must resolve to `AnimationResourceFormat::kMat4x4F32`.
16. `fbx_joint_meta_table_offset == 0` means no FBX-specific pre/post rotation metadata was authored.
17. FBX local transform evaluation order is fixed and deterministic: `local = T * PreR * R * PostR * S`. (Parent application uses persisted `ScaleInheritanceMode` and `inherit_type` without fallback).
18. Strict parity mode must hard-fail on unsupported FBX inherit/evaluate policies.
19. `*_offset_bytes` fields in `MorphTargetRecord` are offsets into the corresponding packed resource in `MorphTargetSetAssetDesc`. Each offset must be 16-byte aligned.
20. Dense span size is `dense_vertex_count * element_stride` (stride = 12 bytes for F32x3).
21. Sparse spans must satisfy `sparse_count` identity, and delta span length equals `sparse_count * element_stride`.
22. All spans must be fully in-bounds of their packed resource payload.
23. `default_weights_offset != 0` requires `default_weight_count == target_count`.
24. Every `name_offset` must resolve within the target-name string table bounds.
25. `payload_stride_bytes`, `payload_component_size_bytes`, and `payload_component_count` must be non-zero for cache formats that carry per-frame vertex payloads.
26. `kAlembicVertexCache` payloads must store frames sequentially; decoded payload size must equal `frame_count * payload_stride_bytes`, and `payload_stride_bytes == vertex_count * payload_component_count * payload_component_size_bytes`.
27. `kVertexAnimationTexture` payloads require valid `cache_layout_id` for VAT and must include texel-compatible component sizing; strict mode rejects inferred defaults.
28. `kMlDeformerDelta` payloads require explicit `cache_layout_id` and topology/hash match to source mesh; strict mode rejects opaque blobs without declared layout.
29. `payload_resource_index` must resolve to a deformation payload compatible with `format`.
30. `aux_payload_resource_index != kNoResourceIndex` is required when the selected `cache_layout_id` declares a secondary stream.
31. `sample_rate` and `frame_count` must be finite and non-zero for time-sampled cache playback.
32. `DeformationLodMapAssetDesc.record_table_offset` is descriptor-local and `record_count` rows must be fully in-bounds.
33. deformation resource compression descriptor violations against §2.2 canonical size semantics.

## 5. Domain: Modular Physics Integration

### 5.1 Interop Resources

```cpp
enum class PhysicsBackend : uint8_t {
  kJolt = 1,
  kPhysX = 2,
};

enum class PhysicsResourceFormat : uint8_t {
  kJoltShapeBinary = 1,
  kJoltConstraintBinary = 2,
  kJoltSoftBodyBinary = 3,
  kPhysXCookedConvex = 11,
  kPhysXCookedTriangleMesh = 12,
  kPhysXConstraintBlob = 13,
  kPhysXArticulationBlob = 14,
  kPhysXClothBlob = 15,
};

#pragma pack(push, 1)
struct PhysicsResourceDesc {
  OffsetT data_offset = 0;
  DataBlobSizeT size_bytes = 0;
  PhysicsResourceFormat format = PhysicsResourceFormat::kJoltShapeBinary;
  uint32_t backend_mask = 0; // bitmask of PhysicsBackend
  PayloadCompressionDesc compression = {};
  uint64_t content_hash = 0;
};
#pragma pack(pop)
// ABI: sizeof(PhysicsResourceDesc) is canonicalized in PakFormat.h with static_assert.
```

### 5.2 Skeleton Rigid Bindings

```cpp
enum class PhysicsDeformationSolverType : uint8_t {
  kCloth = 1,
  kRope = 2,
  kChain = 3,
  kSoftBody = 4,
};

#pragma pack(push, 1)
struct PhysicsSkeletonBindingAssetDesc {
  AssetHeader header;
  AssetKey skeleton_asset_key = {};
  uint32_t body_binding_count = 0;
  uint32_t constraint_binding_count = 0;
  OffsetT body_binding_table_offset = 0; // PhysicsBoneBodyBindingRecord[]
  OffsetT constraint_binding_table_offset = 0; // PhysicsBoneConstraintBindingRecord[]
  uint32_t backend_mask = 0; // bitmask of PhysicsBackend
  uint8_t reserved[105] = {};
};
#pragma pack(pop)
// ABI: sizeof(PhysicsSkeletonBindingAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct PhysicsBoneBodyBindingRecord {
  AssetKey joint_key = {};
  ResourceIndexT shape_resource_index = kNoResourceIndex;
  AssetKey material_asset_key = {};
  float mass = 0.0F;
  float local_translation[3] = { 0.0F, 0.0F, 0.0F };
  float local_rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  uint32_t flags = 0; // kKinematicFromAnim/kSimulated/etc.
  uint32_t backend_mask = 0;
};
#pragma pack(pop)
// ABI: sizeof(PhysicsBoneBodyBindingRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct PhysicsBoneConstraintBindingRecord {
  AssetKey parent_joint_key = {};
  AssetKey child_joint_key = {};
  ResourceIndexT constraint_resource_index = kNoResourceIndex;
  AssetKey constraint_key = {};
  uint32_t flags = 0;
  uint32_t backend_mask = 0;
};
#pragma pack(pop)
// ABI: sizeof(PhysicsBoneConstraintBindingRecord) is canonicalized in PakFormat.h with static_assert.
```

### 5.3 Simulated Deformation Profiles

```cpp
#pragma pack(push, 1)
struct PhysicsBackendTuningRecord {
  PhysicsBackend backend = PhysicsBackend::kJolt;
  uint32_t param_id = 0; // Core Meta id for backend-specific tuning field
  float value_f32 = 0.0F;
  uint32_t op_flags = 0; // bit0: replace, bit1: add, bit2: multiply, bit3: clamp
};
#pragma pack(pop)
// ABI: sizeof(PhysicsBackendTuningRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct ClothSolverBaselineRecord {
  ResourceIndexT constraint_set_resource = kNoResourceIndex; // physics_resource_table
  ResourceIndexT collision_set_resource = kNoResourceIndex; // physics_resource_table
  float drag = 0.0F;
  float lift = 0.0F;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(ClothSolverBaselineRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct RopeSolverBaselineRecord {
  ResourceIndexT segment_constraint_resource = kNoResourceIndex; // physics_resource_table
  ResourceIndexT collision_set_resource = kNoResourceIndex; // physics_resource_table
  float stretch_stiffness = 0.0F;
  float bend_stiffness = 0.0F;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(RopeSolverBaselineRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct ChainSolverBaselineRecord {
  ResourceIndexT joint_constraint_resource = kNoResourceIndex; // physics_resource_table
  ResourceIndexT collision_set_resource = kNoResourceIndex; // physics_resource_table
  float angular_limit_scale = 1.0F;
  float drive_strength = 0.0F;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(ChainSolverBaselineRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct SoftBodySolverBaselineRecord {
  ResourceIndexT soft_body_resource = kNoResourceIndex; // physics_resource_table
  ResourceIndexT collision_set_resource = kNoResourceIndex; // physics_resource_table
  float volume_stiffness = 0.0F;
  float pressure = 0.0F;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(SoftBodySolverBaselineRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationPhysicsProfileRecord {
  PhysicsDeformationSolverType solver_type = PhysicsDeformationSolverType::kCloth;
  uint32_t solver_frequency = 0;
  float damping = 0.0F;
  float stiffness = 0.0F;
  float friction = 0.0F;
  uint32_t collision_mode = 0; // discrete/continuous/virtual particles
  uint32_t self_collision_tier = 0;
  uint32_t tether_mode = 0;
  float tether_max_distance = 0.0F;
  uint32_t backend_mask = 0;
};
#pragma pack(pop)
// ABI: sizeof(DeformationPhysicsProfileRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationPhysicsProfileAssetDesc {
  AssetHeader header;
  AssetKey profile_key = {};
  PhysicsDeformationSolverType solver_type = PhysicsDeformationSolverType::kCloth;
  uint32_t backend_mask = 0;
  DeformationPhysicsProfileRecord default_profile = {};
  uint32_t cloth_baseline_count = 0;
  OffsetT cloth_baseline_table_offset = 0; // ClothSolverBaselineRecord[cloth_baseline_count]
  uint32_t rope_baseline_count = 0;
  OffsetT rope_baseline_table_offset = 0; // RopeSolverBaselineRecord[rope_baseline_count]
  uint32_t chain_baseline_count = 0;
  OffsetT chain_baseline_table_offset = 0; // ChainSolverBaselineRecord[chain_baseline_count]
  uint32_t soft_body_baseline_count = 0;
  OffsetT soft_body_baseline_table_offset = 0; // SoftBodySolverBaselineRecord[soft_body_baseline_count]
  uint32_t backend_tuning_count = 0;
  OffsetT backend_tuning_table_offset = 0; // PhysicsBackendTuningRecord[backend_tuning_count]
  uint8_t reserved[69] = {};
};
#pragma pack(pop)
// ABI: sizeof(DeformationPhysicsProfileAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationAttachmentRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_node_key = {};
  AssetKey skeleton_joint_key = {};
  AssetKey physics_profile_asset_key = {};
  ResourceIndexT pin_map_resource_index = kNoResourceIndex; // references deformation_resource_table
  uint32_t pin_count = 0;
};
#pragma pack(pop)
// ABI: sizeof(DeformationAttachmentRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationAttachmentAssetDesc {
  AssetHeader header;
  AssetKey target_scene_asset_key = {};
  uint32_t attachment_count = 0;
  OffsetT attachment_table_offset = 0; // DeformationAttachmentRecord[attachment_count]
  uint8_t reserved[121] = {};
};
#pragma pack(pop)
// ABI: sizeof(DeformationAttachmentAssetDesc) is canonicalized in PakFormat.h with static_assert.
```

### 5.4 Decoupled Tuning Overrides

To preserve fast iteration for artists and technical designers, physics authoring is split into two layers:

1. Baseline Cooked Physics (PAK authoritative defaults)
2. Runtime Tuning Override Layer (hot-iterable)
Overrides must not mutate topology or identity. Deterministic application order: baseline -> project override -> user/session override.

```cpp
enum class PhysicsTuningTargetType : uint8_t {
  kBody = 1,
  kConstraint = 2,
  kDeformationProfile = 3,
  kRootMotionPolicy = 4,
};

enum class PhysicsOverrideValueType : uint8_t {
  kFloat = 1,
  kUInt = 2,
  kBool = 3,
};

#pragma pack(push, 1)
struct PhysicsTuningOverrideAssetDesc {
  AssetHeader header;
  AssetKey target_skeleton_key = {};
  uint32_t record_count = 0;
  OffsetT record_table_offset = 0; // PhysicsTuningOverrideRecord[record_count]
  uint32_t precedence = 0; // lower applies first
  uint32_t flags = 0; // shipping_allowed, dev_only, etc.
  uint8_t reserved[109] = {};
};
#pragma pack(pop)
// ABI: sizeof(PhysicsTuningOverrideAssetDesc) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct PhysicsTuningOverrideRecord {
  PhysicsTuningTargetType target_type = PhysicsTuningTargetType::kBody;
  PhysicsOverrideValueType value_type = PhysicsOverrideValueType::kFloat;
  AssetKey target_key = {}; // joint_key/constraint_key/profile_key/root_motion_policy_key by target_type
  uint32_t field_id = 0; // Core Meta id of tunable field
  float value_f32 = 0.0F;
  uint32_t value_u32 = 0; // used when value_type is kUInt/kBool
  uint32_t backend_mask = 0; // optional backend scope; 0 means derive from target domain
  uint32_t op_flags = 0; // bit0: replace, bit1: add, bit2: multiply, bit3: clamp
};
#pragma pack(pop)
// ABI: sizeof(PhysicsTuningOverrideRecord) is canonicalized in PakFormat.h with static_assert.
```

### 5.5 Domain Validation Rules

Mandatory hard-fail diagnostics for Modular Physics Integration:

1. backend unsupported physics profile.
2. physics tuning override attempts topology mutation.
3. physics tuning override references unknown target key.
4. physics tuning override references non-whitelisted `field_id`.
5. physics bone constraint and body bindings must not target joints that evaluate to non-uniform, skewed (sheared) global matrices.
6. `PhysicsResourceDesc.backend_mask` must include at least one backend compatible with `format`.
7. `pin_map_resource_index` must resolve to `DeformationResourceFormat::kAttachmentPinMapU32x2`.
8. every `PhysicsBackendTuningRecord.param_id` must be whitelisted for its backend and solver type.
9. `PhysicsBoneBodyBindingRecord.material_asset_key` must resolve to `PhysicsMaterialAssetDesc`.
10. solver-specific baseline tables must match `solver_type` and be non-empty for the selected solver.
11. solver baseline resource entries must resolve to backend-compatible `PhysicsResourceFormat` payloads for all enabled backends.
12. `PhysicsTuningOverrideRecord.target_type == kConstraint` must resolve uniquely by `(constraint_key, backend)` where backend is defined by `backend_mask` (or derived from target domain when `backend_mask == 0`).
13. `shape_resource_index` must resolve to `physics_resource_table`.
14. `backend_mask` on each body/constraint binding must be a subset of owning `PhysicsSkeletonBindingAssetDesc.backend_mask`.
15. If a logical constraint targets multiple backends with incompatible payload formats, cooker must emit backend-split `PhysicsBoneConstraintBindingRecord` rows sharing the same `constraint_key`, each with exclusive `backend_mask` and backend-matching `constraint_resource_index`.
16. Rows sharing the same logical `constraint_key` are allowed only for backend-split persistence; such rows must have mutually exclusive `backend_mask`, identical `parent_joint_key/child_joint_key`, and backend-compatible resource formats.
17. `physics_profile_asset_key` in attachments must resolve to `DeformationPhysicsProfileAssetDesc.profile_key`.
18. `backend_tuning_table_offset` is descriptor-local and bounded by profile payload size.
19. `backend_tuning_count == 0` is allowed only when one `default_profile` serves all enabled backends.
20. `pin_count` must be within resolved pin-map payload bounds in `DeformationAttachmentRecord`.
21. `DeformationAttachmentAssetDesc.attachment_table_offset` is descriptor-local and `attachment_count` rows must be fully in-bounds.
22. `DeformationAttachmentAssetDesc.target_scene_asset_key` must resolve to exactly one scene asset.
23. `PhysicsTuningOverrideRecord.value_type` must match the whitelist type domain for `field_id`; mismatched typed lanes (`value_f32` vs `value_u32`) are hard-fail.
24. `PhysicsTuningTargetType::kRootMotionPolicy` overrides must resolve only to persisted root-motion policy keys.
25. `kAttachmentPinMapU32x2` payload contract is strict: each pin entry is `(vertex_index, attachment_index)`; both lanes must be in-bounds for the referenced mesh/profile attachments.
26. physics resource compression descriptor violations against §2.2 canonical size semantics.
27. when `PhysicsTuningOverrideRecord.backend_mask != 0`, it must be a subset of the resolved target backend domain.
## 6. Domain: Scene Bindings & Render Contacts

### 6.1 Geometry Bindings

```cpp
#pragma pack(push, 1)
struct SkinnedRenderableBindingRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_node_key = {};
  AssetKey mesh_asset_key = {};
  AssetKey skin_asset_key = {};
  AssetKey morph_target_set_key = {};
  AssetKey deformation_lod_map_asset_key = {};
  AssetKey render_contract_asset_key = {};
  AssetKey deformation_attachment_asset_key = {}; // optional, resolves to DeformationAttachmentAssetDesc
};
#pragma pack(pop)
// ABI: sizeof(SkinnedRenderableBindingRecord) is canonicalized in PakFormat.h with static_assert.
```

### 6.2 Runtime Component Records

Scene component table linkage is explicit and persisted through `SceneComponentTableDesc.component_type` ids in Core Meta.
Each record below is stored in its own component table and resolved by `(target_node_key, node_index)` using key-first validation.

```cpp
#pragma pack(push, 1)
struct AnimationBindingRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_node_key = {};
  AssetKey animation_clip_key = {};
  AssetKey retarget_profile_asset_key = {}; // optional, required for cross-skeleton playback
  AssetKey root_motion_policy_asset_key = {}; // optional, required when root-motion policy flag is set
  AssetKey root_motion_policy_key = {}; // key of selected RootMotionPolicyRecord inside policy asset
  float playback_speed = 1.0F;
  float start_time_seconds = 0.0F;
  uint32_t flags = 0; // bit0: autoplay, bit1: loop, bit2: additive, bit3: root_motion_enabled
};
#pragma pack(pop)
// ABI: sizeof(AnimationBindingRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct MorphWeightsOverrideRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_node_key = {};
  AssetKey morph_target_set_key = {};
  ResourceIndexT weights_resource_index = kNoResourceIndex; // animation_resource_table, kWeightsF32
  uint32_t weights_count = 0;
};
#pragma pack(pop)
// ABI: sizeof(MorphWeightsOverrideRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct DeformationCacheBindingRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_node_key = {};
  AssetKey cache_asset_key = {};
  float start_time_offset = 0.0F;
  float playback_speed = 1.0F;
  uint32_t playback_mode = 0; // 0=Loop, 1=Clamp, 2=PingPong
};
#pragma pack(pop)
// ABI: sizeof(DeformationCacheBindingRecord) is canonicalized in PakFormat.h with static_assert.
```

### 6.3 Root Motion Authority

Persist ownership policies: animation-owned root motion, controller/physics-owned root motion, blended ownership.

```cpp
enum class RootMotionSpace : uint32_t {
  kLocal = 0,
  kModel = 1,
  kWorld = 2,
};

enum class RootMotionChannelSelector : uint32_t {
  kFromFlaggedChannel = 0, // use channel marked with root_motion_channel bit
  kFromRootNodeTRS = 1,    // derive from root-node TRS channels
};

#pragma pack(push, 1)
struct RootMotionPolicyRecord {
  AssetKey policy_key = {};
  AssetKey animation_clip_key = {};
  AssetKey root_node_key = {};
  RootMotionChannelSelector translation_channel_selector = RootMotionChannelSelector::kFromFlaggedChannel;
  RootMotionChannelSelector rotation_channel_selector = RootMotionChannelSelector::kFromFlaggedChannel;
  RootMotionSpace extraction_space = RootMotionSpace::kModel;
  uint32_t axis_basis_id = 0; // Core Meta coordinate basis id
  uint32_t policy_flags = 0;
  float translation_scale = 1.0F;
  float rotation_scale = 1.0F;
};
#pragma pack(pop)
// ABI: sizeof(RootMotionPolicyRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct RootMotionPolicyAssetDesc {
  AssetHeader header;
  uint32_t record_count = 0;
  OffsetT record_table_offset = 0; // RootMotionPolicyRecord[record_count]
  uint8_t reserved[145] = {};
};
#pragma pack(pop)
// ABI: sizeof(RootMotionPolicyAssetDesc) is canonicalized in PakFormat.h with static_assert.

// Root-motion policy flags (Core Meta ids, strict):
// bit0 = translation_enabled, bit1 = rotation_enabled, bit2 = physics_authoritative, bit3 = controller_authoritative
```

### 6.4 Material Animation Contracts

```cpp
enum class MaterialParamType : uint8_t {
  kScalar = 1,
  kVec2 = 2,
  kVec3 = 3,
  kVec4 = 4,
  kColorLinear = 5,
  kBool = 6,
  kTextureSlot = 7,
  kKeywordToggle = 8,
};

#pragma pack(push, 1)
struct MaterialAnimationTrackRecord {
  uint32_t param_name_offset = 0;
  MaterialParamType param_type = MaterialParamType::kScalar;
  uint32_t sampler_index = 0;
  uint32_t target_binding_slot = 0; // deterministic material binding slot
  uint32_t flags = 0; // bit0: clamp_to_param_domain, bit1: runtime_linearize_srgb
};
#pragma pack(pop)
// ABI: sizeof(MaterialAnimationTrackRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct MaterialAnimationBindingRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey target_scene_asset_key = {};
  AssetKey target_node_key = {};
  AssetKey material_asset_key = {};
  AssetKey animation_clip_key = {};
  uint32_t flags = 0; // bit0: auto-bind clip material tracks by string name
};
#pragma pack(pop)
// ABI: sizeof(MaterialAnimationBindingRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationRenderContractRecord {
  uint32_t required_shader_features = 0;
  uint32_t required_buffer_layout_id = 0;
  uint32_t max_supported_bones = 0;
  uint32_t max_supported_morph_targets = 0;
  uint32_t motion_vector_mode = 0;
  uint32_t flags = 0;
};
#pragma pack(pop)
// ABI: sizeof(AnimationRenderContractRecord) is canonicalized in PakFormat.h with static_assert.

#pragma pack(push, 1)
struct AnimationRenderContractAssetDesc {
  AssetHeader header;
  uint32_t record_count = 0;
  OffsetT record_table_offset = 0; // AnimationRenderContractRecord[record_count]
  uint8_t reserved[145] = {};
};
#pragma pack(pop)
// ABI: sizeof(AnimationRenderContractAssetDesc) is canonicalized in PakFormat.h with static_assert.
```

Material param animation format contract (strict):

1. `kScalar` -> scalar formats only (`kScalarF32`, `kScalarF16`, `kScalarUNorm8`, `kScalarUNorm16`).
2. `kVec2` -> `kVec2F32` only.
3. `kVec3` -> `kVec3F32` or `kVec3F16`.
4. `kVec4` / `kColorLinear` -> `kVec4F32` only.
5. `kBool` / `kKeywordToggle` -> scalar formats only and `AnimationInterpolation::kStep` only.
6. `kTextureSlot` -> scalar formats only and `AnimationInterpolation::kStep` only; sampled value is rounded to nearest integer slot index.
7. No implicit coercion between scalar/vector/bool/slot domains in strict mode.

### 6.5 Domain Validation Rules

Mandatory hard-fail diagnostics for Scene Bindings & Contracts:

1. material param binding unresolved.
2. render contract unsupported feature.
3. material animation track table overflow or unresolved sampler.
4. material animation binding references unresolved scene/node keys.
5. `MaterialAnimationTrackRecord` `param_name_offset` must resolve to an exactly matched active parameter inside the targeted `MaterialAsset`.
6. `MaterialAnimationTrackRecord.target_binding_slot` must resolve to exactly one material binding target.
7. key/index mismatch in scene animation bindings must hard-fail (no implicit remap).
8. root-motion-enabled animation bindings must resolve valid `RootMotionPolicyAssetDesc` and exactly one matching `RootMotionPolicyRecord` by `(policy_key, animation_clip_key)`.
9. `render_contract_asset_key` must resolve to `AnimationRenderContractAssetDesc` and contain at least one compatible record.
10. `weights_resource_index` must resolve to `AnimationResourceFormat::kWeightsF32` with exact `weights_count`.
11. `MaterialAnimationBindingRecord` uniqueness on `(target_scene_asset_key, target_node_key, material_asset_key, node_index, animation_clip_key)` must hold.
12. root-motion extraction contract must resolve exactly one translation and one rotation source channel as selected by `RootMotionPolicyRecord`.
13. `RootMotionChannelSelector` and `RootMotionSpace` enum values must be valid and supported by runtime evaluator.
14. `RootMotionPolicyRecord` selectors must not request channels disallowed by clip `root_motion_flags`.
15. `SkinnedRenderableBindingRecord` must not persist dynamic runtime state fields (for example joint matrix buffer/resource indices).
16. `MaterialAnimationTrackRecord` channels targeting `kVec4`/`kColorLinear` parameters must use `AnimationResourceFormat::kVec4F32`.
17. `MaterialAnimationTrackRecord.param_type` must match the strict type/format matrix (`scalar/vec2/vec3/vec4/bool/slot`) with no implicit coercion.
18. Scene-bound records must carry both `target_node_key` and `node_index`. `target_node_key` is authoritative; `node_index` is a cache hint only. Loader must verify key/index coherence and hard-fail on mismatch.
19. Material binding array ordering is canonical: ascending `(lod_index, submesh_index)`.
20. `root_node_key` must resolve to the bound skeleton root node for the owning animation binding.
21. `translation_channel_selector` and `rotation_channel_selector` must each resolve to exactly one channel.
22. `extraction_space` and `axis_basis_id` are mandatory for deterministic cross-backend replay.
23. Asset-key ownership mapping is explicit and mandatory: `render_contract_asset_key` -> `AnimationRenderContractAssetDesc`, `deformation_lod_map_asset_key` -> `DeformationLodMapAssetDesc`, `root_motion_policy_asset_key` -> `RootMotionPolicyAssetDesc`, `root_motion_policy_key` -> `RootMotionPolicyRecord.policy_key`, `retarget_profile_asset_key` -> `RetargetProfileAssetDesc`.
24. Geometry schema deltas: `MeshDesc`/`GeometryAssetDesc` payload rules must serialize explicit keys/flags in fixed order and validate presence according to mesh type. Shared packed morph buffer usage is required (no per-target resource handle fan-out).
25. `deformation_lod_map_asset_key` in `SkinnedRenderableBindingRecord` is required when `morph_target_set_key` is present.
26. `deformation_attachment_asset_key` resolves only to `DeformationAttachmentAssetDesc` and `target_scene_asset_key` must match owning scene.
27. `retarget_profile_asset_key` in `AnimationBindingRecord` must resolve to `RetargetProfileAssetDesc` when source and target skeleton keys differ.
28. `kBool`, `kKeywordToggle`, and `kTextureSlot` material tracks must use `AnimationInterpolation::kStep`.
29. `AnimationBindingRecord` root-motion policy key and retarget profile key must not be silently ignored; unresolved optional keys that are non-zero are hard-fail.
30. camera/light target-path channels must resolve to nodes containing the required camera/light component.
31. when `AnimationBindingRecord.flags.bit3 (root_motion_enabled)` is set, both `root_motion_policy_asset_key` and `root_motion_policy_key` are required and must resolve.
32. `RootMotionPolicyAssetDesc.record_table_offset` is descriptor-local and `record_count` rows must be fully in-bounds.
33. `RootMotionPolicyRecord.policy_key` values must be unique within the owning policy asset.

## 7. Runtime Evaluator Specification

### 7.1 Resolver & Load Contract

1. load v7 footer/tables.
2. resolve animation/deformation/physics resources.
3. resolve scene bindings and scene component tables for animation/deformation/material/physics attachment records.
4. resolve retarget, root-motion policy, deformation attachment, and render contracts.
5. resolve key/index mappings using canonical rule: stable key resolution first, then index verification.

### 7.2 Frame Execution Order

1. sample clip samplers, events, and custom curves.
2. evaluate channel targets (TRS, morph, camera/light, material params).
3. apply retarget profile (when bound) and build deterministic joint local/global transforms.
4. apply root-motion extraction policy and ownership arbitration.
5. compute skinning + deformation (morph, cache, attachments).
6. pre-physics sync.
7. physics step.
8. post-physics sync/writeback.

### 7.3 Determinism Contract

1. explicit interpolation semantics.
2. quaternion continuity and normalization.
3. NaN/inf sanitization.
4. explicit quantization/compression decode policy.
5. scale inheritance evaluation must follow persisted `scale_inheritance_mode` exactly.
6. normalized integer track dequantization must apply per-component `dequant_scale/dequant_offset`.
7. quaternion tracks must be renormalized after dequantization before interpolation.
8. typed physics tuning overrides apply in deterministic precedence order (`baseline -> project override -> user/session override`) with no type coercion.

## 8. Testing Matrix & Acceptance

### 8.1 Unit Testing Hooks

1. ABI/serialization tests for all structs.
2. interpolation/curve/deformation correctness tests.
3. compression/quantization contract tests.
4. normalized integer track dequantization tests (U8/U16/SNorm16 paths).
5. scale inheritance mode matrix concatenation tests.
6. clip string table bounds/name offset validation tests.
7. canonical footer field-order/offsetof ABI tests.
8. physics resource format/backend compatibility matrix tests.
9. key/index coherence tests for all scene-bound records.
10. quantized format domain + quaternion renormalization tests.
11. skeleton FBX joint-meta linkage and bounds tests.
12. physics bone-body material key resolution tests.
13. render/deformation contract asset-key domain resolution tests.
14. morph-weights resource index format/count validation tests.
15. deformation cache topology-hash/layout compatibility tests.
16. root-motion selector enum legality and fallback rejection tests.
17. clip root-motion flag vs policy-selector compatibility tests.
18. inverse-bind resource format enforcement tests (`kMat4x4F32` required).
19. material vector/color track format enforcement tests (`kVec4F32` required).
20. material param-type compatibility tests (scalar/vec2/vec3/vec4/bool/slot strict mapping).
21. `PayloadCompressionDesc` canonical size semantics tests (`codec == kNone` and `codec != kNone` cases).
22. sampler monotonic-time and cubic-spline triplet-layout validation tests.
23. camera/light target-path format/domain validation tests (near/far, color/intensity/range).
24. root-motion policy asset + record resolution tests (`policy_key`, `animation_clip_key` uniqueness).
25. retarget profile binding tests (`AnimationBindingRecord.retarget_profile_asset_key` linkage and skeleton compatibility).
26. deformation cache stride/component/layout validation tests (`payload_stride_bytes`, `cache_layout_id`, aux stream requirements).
27. deformation attachment asset bounds/linkage tests (`attachment_count`, scene ownership, pin-map domain).
28. backend-split `constraint_key` invariants tests (mutually exclusive masks, identical joint pair).
29. typed physics override application tests (`value_type`, `field_id` whitelist, typed lane usage).
30. skinned render binding linkage tests for `render_contract_asset_key` and `deformation_lod_map_asset_key`.
31. physics tuning override `backend_mask` domain/subset validation tests.

### 8.2 Integration Pipeline Tests

1. glTF skin+morph+animation parity.
2. FBX skeletal+blendshape+animation parity.
3. material animation playback parity.
4. LOD transition + deformation parity.
5. ragdoll/cloth/rope/chain behavior.
6. physics tuning override hot-reload without geometry recook.
7. bindless descriptor budget tests for high-count morph target rigs.
8. FBX scaled hierarchy parity tests (inheritance-mode sensitive scenes).
9. targeted single-morph animation channel playback tests.
10. Physics skeleton skew rejection test.
11. Missing scene component mapping tests.
12. key-first resolver mismatch tests.
13. material binding-slot domain tests for multi-LOD/submesh bindings.
14. pin-map resource resolution and format compatibility tests.
15. Retarget chain string-table bounds tests.
16. Material track binding-slot disambiguation tests (multi-material clip).
17. Packed morph span alignment and bounds tests.
18. root-motion policy linkage and authority handoff tests.
19. backend-specific deformation tuning record application tests.
20. Physics resource format/backend mask matrix tests.
21. FBX pre/post rotation parity tests (joint-meta driven).
22. solver-specific baseline table presence/selection tests by solver type.
23. root-motion channel selector determinism tests.
24. material animation uniqueness tests across repeated material instances.
25. solver-baseline resource format compatibility tests against backend mask.
26. clip root-motion-flag contract enforcement tests.
27. Physics tuning override disambiguation test by `constraint_key` when multiple constraints share the same joint pair.
28. Dual-backend skeleton-physics cook bifurcation test (shared logical `constraint_key`, backend-exclusive records/resources).
29. FBX camera/light animation import->cook->playback parity tests using explicit camera/light target paths.
30. material bool/keyword/texture-slot step-track behavior tests (strict no-interpolation).
31. retargeted playback integration tests with explicit `retarget_profile_asset_key` binding.
32. root-motion policy asset indirection tests (binding key -> policy asset -> record selection).
33. deformation attachment scene ownership and pin-map bounds tests.
34. VAT and ML deformer cache layout completeness tests (no inferred defaults in strict mode).
35. compression descriptor mismatch rejection tests (decoded size mismatch, wrong `size_bytes`).
36. mixed camera/light/mesh animation clips with deterministic evaluation-order replay tests.
37. root-motion-enabled binding rejection tests when policy asset/key fields are missing or unresolved.

### 8.3 Dual-Backend Divergence Checks

1. same scenes on Jolt and PhysX.
2. bounded divergence checks.
3. root motion + controller + physics interplay.
4. baseline+override deterministic replay (same overrides => same result).

## 9. Implementation Plan (Direct v7)

1. update `v7::PakFooter` with animation/deformation tables.
2. add all new enums/ids to Core Meta `.inc`.
3. add new descriptors under `pak::v7`.
4. update serio readers/writers.
5. extend glTF and FBX importers to populate all schema (including camera/light channels and strict material param-track typing).
6. extend runtime animation/deformation/material animation systems (retarget binding, root-motion policy assets, attachment assets).
7. extend Jolt/PhysX bridge for persisted profiles.
8. implement physics tuning override loader and application pipeline.
9. wire scene component tables for new record types and enforce key-first resolution for every scene-bound row.
10. enable strict parity mode and CI gates.

## 10. External Baseline References

1. Unreal Animation Curves: <https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-curves-in-unreal-engine>
2. Unreal IK Rig Retargeting: <https://dev.epicgames.com/documentation/en-us/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine>
3. Unreal Animation Compression: <https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-compression-in-unreal-engine>
4. Unreal Chaos Cloth Tool Overview: <https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine>
5. Unreal Cable Components: <https://dev.epicgames.com/documentation/en-us/unreal-engine/cable-components-in-unreal-engine>
6. Unity Animation Curves / Clip Properties: <https://docs.unity3d.com/Manual/animeditor-AnimationCurves.html>
7. Unity Blend Shapes: <https://docs.unity3d.com/Manual/BlendShapes.html>
8. Unity Retargeting Humanoid Animations: <https://docs.unity3d.com/Manual/Retargeting.html>
9. Unity Mesh Compression (import): <https://docs.unity3d.com/Manual/class-Mesh.html>
10. Unity Cloth component: <https://docs.unity3d.com/Manual/class-Cloth.html>
11. Unity LOD Group / Cross-fade: <https://docs.unity3d.com/Manual/class-LODGroup.html>
12. NVIDIA PhysX Articulations: <https://nvidia-omniverse.github.io/PhysX/physx/5.1.0/docs/Articulations.html>
13. Jolt Physics Documentation: <https://jrouwe.github.io/JoltPhysicsDocs/>
