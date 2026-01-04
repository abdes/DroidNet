# Oxygen Engine Shader System — Refactoring Specification (DX12 + Bindless)

Date: 2026-01-04

## Non-negotiable constraints

1. **No backward-compat shims.** Every call site and shader will be migrated to the final API and final file layout.
2. **Single source of truth for shader binaries.** The engine loads one artifact format. No dual “CMake .cso” vs “runtime archive” paths.
3. **Human-readable shader requests at PSO call sites.** Render code must express: stage, path, entry point, defines. Render code must never traffic in hashes.
4. **GPU contracts are authoritative and identical in C++ and HLSL.** No re-declaration of engine structs in entry shaders.
5. **Deterministic paths.** No absolute paths embedded in shader metadata; no hard-coded workspace roots.
6. **Strong types for critical identities/indices.** C++ APIs must use Oxygen strong types (`NamedType` skills) and existing core types (`ShaderVisibleIndex`, bindless handles, etc.) for indices/handles and other critical identities. Raw integers are not acceptable for those. Plain strings are acceptable for shader paths/entry points/define text as long as they are strictly validated and canonicalized.

## 1) Authoritative binding & root signature contract (D3D12)

This is the bindless root signature contract that all passes share.

**Source of truth (generated):** `src/Oxygen/Core/Bindless/Generated.RootSignature.h`.

**Required root signature flags (must remain enabled):**

- `D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`
- `D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED`

### 1.1 Root parameters (indices are stable)

| Root param | Index | Kind | Binding | Notes |
| --- | ---: | --- | --- | --- |
| Bindless SRV table | 0 | Descriptor table | `t0, space0, unbounded` | Present for RS compatibility and legacy declarations. Access is via SM 6.6 `ResourceDescriptorHeap[]`. |
| Sampler table | 1 | Descriptor table | `s0, space0, 256` | Sampler heap; also accessed via SM 6.6 `SamplerDescriptorHeap[]` when enabled. |
| SceneConstants | 2 | Root CBV | `b1, space0` | Direct GPUVA root CBV. Layout is fixed (see §2). |
| Root constants | 3 | Root constants | `b2, space0` | **Final:** two 32-bit constants (see §1.2). (Rename generated enum entry from `kDrawIndex` → `kRootConstants`.) |

### 1.2 Root constants layout (register b2, space0)

Root constants are a fixed ABI. Final layout:

```hlsl
cbuffer RootConstants : register(b2, space0)
{
  uint g_DrawIndex;            // used by graphics draws; undefined for dispatch
  uint g_PassConstantsIndex;   // shader-visible descriptor heap index for pass constants CBV; use K_INVALID_BINDLESS_INDEX when none
}
```

**C++ strong typing requirement:**

- `g_DrawIndex` is exposed as `DrawIndex` (strong type).
- `g_PassConstantsIndex` is exposed as `oxygen::ShaderVisibleIndex`.
- Sentinel values must use the engine-defined constants (`oxygen::kInvalidShaderVisibleIndex`), not raw integers at call sites.

**Binding rules:**

- Graphics passes set `g_PassConstantsIndex` once per pass, and set `g_DrawIndex` per draw.
- Compute passes set `g_PassConstantsIndex` once per dispatch; `g_DrawIndex` is unused.
- Sentinel: `K_INVALID_BINDLESS_INDEX` means “not provided”. Shaders must branch on it before dereferencing.

## 2) Authoritative GPU contracts (C++ ⇄ HLSL)

All contracts below are authoritative and must be mirrored exactly.

**C++ source of truth:**

- `src/Oxygen/Renderer/Types/SceneConstants.h` (`SceneConstants::GpuData`, size = 176 bytes)
- `src/Oxygen/Renderer/Types/DrawMetadata.h` (`DrawMetadata`, size = 64 bytes)
- `src/Oxygen/Renderer/Types/MaterialConstants.h` (`MaterialConstants`, size = 80 bytes)

### 2.1 Sentinel values

- `oxygen::engine::kInvalidDescriptorSlot` is the only invalid bindless slot value.
- `oxygen::kInvalidBindlessIndex` is the invalid sentinel value shared by the generated bindless layer.
- `oxygen::kInvalidShaderVisibleIndex` is the typed invalid sentinel for `oxygen::ShaderVisibleIndex`.

**Asset index contract (PAK, normative):**

- There is no “invalid material” and no “invalid texture”. Material/texture indices stored in assets are always valid.
- Index `0` is reserved for engine-provided fallback assets (default material, white/flat-normal/etc. textures). Shaders must treat `0` as the fallback value, not as “missing”.

**HLSL source of truth:**

Shader sources and includes are fully specified in §5. That section is the
single authoritative layout for shader file locations and include roots.

HLSL uses `K_INVALID_BINDLESS_INDEX` as the only invalid bindless sentinel.

**Important distinction (C++):**

- SceneConstants contains *descriptor slot integers* validated via `oxygen::engine::kInvalidDescriptorSlot` (and the wrapper types in `SceneConstants.h`).
- RootConstants contain *shader-visible indices* typed as `oxygen::ShaderVisibleIndex` and validated via `oxygen::kInvalidShaderVisibleIndex`.

### 2.2 SceneConstants (CBV b1, space0)

**HLSL contract include:** defined in §5.

**ABI:**

```hlsl
// Mirrors oxygen::engine::SceneConstants::GpuData (sizeof = 176)
cbuffer SceneConstants : register(b1, space0)
{
  float4x4 view_matrix;
  float4x4 projection_matrix;
  float3   camera_position;
  uint     frame_slot;
  uint64_t frame_seq_num;
  float    time_seconds;
  uint     _pad0;
  uint     bindless_draw_metadata_slot;
  uint     bindless_transforms_slot;
  uint     bindless_normal_matrices_slot;
  uint     bindless_material_constants_slot;
};
```

**Binding semantics:**

- The four `bindless_*_slot` fields are *shader-visible descriptor heap indices*.
- They point to SRV descriptors for structured buffers, obtained via `ResourceDescriptorHeap[slot]`.
- The renderer must set these per view during scene preparation. Shaders must tolerate the invalid sentinel.

### 2.2.1 SceneConstants boundary rules (future-proofing)

`SceneConstants` is intentionally small and stable: it contains **view invariants** plus **global heap indices** that route shaders to extensible data. It must not become a “feature bucket”.

**Generated bindless tooling (required in shaders):**

Bindless tooling is sourced from the Core module and included as specified in §5.

- Use `K_INVALID_BINDLESS_INDEX` as the only invalid sentinel in HLSL.
- Use the generated domain guards/macros from `Generated.BindlessLayout.hlsl`:
  - `BX_IN_GLOBAL_SRV(idx)` / `BX_TRY_GLOBAL_SRV(idx)`
  - `BX_IN_MATERIALS(idx)` / `BX_TRY_MATERIALS(idx)`
  - `BX_IN_TEXTURES(idx)` / `BX_TRY_TEXTURES(idx)`
  - `BX_IN_SAMPLERS(idx)` / `BX_TRY_SAMPLERS(idx)`

**Heap domain rules for the `SceneConstants` slots (must be enforced by the renderer and asserted in debug shaders):**

- `bindless_draw_metadata_slot` MUST be a **GLOBAL_SRV** index: `BX_IN_GLOBAL_SRV(bindless_draw_metadata_slot)`.
- `bindless_transforms_slot` MUST be a **GLOBAL_SRV** index: `BX_IN_GLOBAL_SRV(bindless_transforms_slot)`.
- `bindless_normal_matrices_slot` MUST be a **GLOBAL_SRV** index: `BX_IN_GLOBAL_SRV(bindless_normal_matrices_slot)`.
- `bindless_material_constants_slot` MUST be a **MATERIALS** index: `BX_IN_MATERIALS(bindless_material_constants_slot)`.

**What stays in `SceneConstants` (and only these categories):**

- View transforms and camera: `view_matrix`, `projection_matrix`, `camera_position`.
- Frame identity/time: `frame_slot`, `frame_seq_num`, `time_seconds`.
- Indirection indices (the four bindless slots above).

**What must NOT go in `SceneConstants` (move it elsewhere):**

1. **Pass-specific configuration and toggles** (debug views, quality/perf switches, technique selection) → put in *PassConstants* (see §1.2 via `g_PassConstantsIndex`).
2. **Pass-local resources** (depth SRV, output UAVs, light lists, transient buffers) → put their heap indices in *PassConstants*.
3. **Large and rapidly evolving view data** (previous-frame matrices, jitter, exposure, shadow cascades, clustered grid parameters) → put in *ViewExtras* buffers referenced by indices stored in *PassConstants*.
4. **Per-draw data** → stays in `DrawMetadata` and is selected by `g_DrawIndex`.
5. **Per-material data** → stays in `MaterialConstants` and textures/samplers are validated with `BX_IN_TEXTURES()` / `BX_IN_SAMPLERS()`.

**Normative fetch patterns (SM 6.6):**

- Draw metadata:
  - Validate slot with `BX_IN_GLOBAL_SRV(slot)`; if invalid, treat as absent.
  - Fetch via `StructuredBuffer<DrawMetadata> buf = ResourceDescriptorHeap[slot]; meta = buf[g_DrawIndex];`
- Pass constants:
  - Treat `g_PassConstantsIndex` as a heap index; validate with `BX_IN_GLOBAL_SRV(g_PassConstantsIndex)`.
  - Fetch via `ConstantBuffer<PassConstants> pc = ResourceDescriptorHeap[g_PassConstantsIndex];`

### 2.3 DrawMetadata (StructuredBuffer, 64-byte stride)

**HLSL contract include:** defined in §5.

**ABI (must match `sizeof(DrawMetadata) == 64`):**

```hlsl
struct DrawMetadata
{
  uint vertex_buffer_index;
  uint index_buffer_index;
  uint first_index;
  int  base_vertex;
  uint is_indexed;
  uint instance_count;
  uint index_count;
  uint vertex_count;
  uint material_handle;
  uint transform_index;
  uint instance_metadata_buffer_index;
  uint instance_metadata_offset;
  uint flags;
  uint padding[3];
};
```

**Notes:**

- `vertex_buffer_index` / `index_buffer_index` are `ShaderVisibleIndex` on the CPU, but ABI is 32-bit unsigned.
- `material_handle` is a stable handle to a *resolved material snapshot* stored in the `MaterialConstants` table; value `0` selects the engine fallback material.
- `instance_metadata_buffer_index` / `instance_metadata_offset` are reserved for per-draw “extras” indirection (see §2.3.1).

**Future-proofing rule (normative):**

- `DrawMetadata` is a **fixed 64-byte header ABI**. Do not grow this struct.
- Any future per-draw expansion (skinning data, meshlet/cluster payloads, picking IDs, extra per-draw material overrides, ray tracing record indices, etc.) MUST be modeled as an *additional indirection buffer* (e.g., `StructuredBuffer<PerDrawExtras>`) referenced by an index/offset carried in `DrawMetadata`, and the heap index for that extras buffer MUST be carried via *PassConstants*.

### 2.3.1 Per-draw overrides (final recommendation)

Oxygen must support both (a) authored material libraries and (b) open-world scale reuse/variation without exploding the number of material snapshots.

**Final ownership rules (normative):**

1. **UV transform (`uv_scale`, `uv_offset`) is a material parameter** and lives in `MaterialConstants`.
   - Authoring tools and engines (UE, Godot, DCC exports) treat UV tiling/offset as a material/shader parameter.
   - This keeps batching stable and avoids per-draw data bloat.

2. **Tint is a per-draw override** and must be supported without minting new material snapshots.
   - This matches common practice: large-scale prop/foliage variation is driven by per-instance/per-primitive data (not a unique material for every object).

**Mechanism (uses existing fields, no `DrawMetadata` growth):**

- If `instance_metadata_buffer_index == K_INVALID_BINDLESS_INDEX`, there are no per-draw overrides.
- Otherwise `instance_metadata_buffer_index` MUST be a **GLOBAL_SRV** heap index that refers to a `StructuredBuffer<PerDrawOverrides>`.
- `instance_metadata_offset` is the element index within that buffer.

**Minimal per-draw payload (required):**

```hlsl
struct PerDrawOverrides
{
  float4 base_color_tint; // multiply with MaterialConstants.base_color; default (1,1,1,1)
};
```

**Concrete shader-side fetch pattern (normative):**

This pattern requires the bindless helper include specified in §5.

```hlsl
// Returns a safe tint multiplier. If overrides are not present/valid, returns (1,1,1,1).
float4 FetchBaseColorTint(in DrawMetadata meta)
{
  // Absent overrides.
  if (meta.instance_metadata_buffer_index == K_INVALID_BINDLESS_INDEX)
  {
    return float4(1.0, 1.0, 1.0, 1.0);
  }

  // Domain validation (required). Invalid domains are treated as absent.
  if (!BX_IN_GLOBAL_SRV(meta.instance_metadata_buffer_index))
  {
    return float4(1.0, 1.0, 1.0, 1.0);
  }

  // Fetch SRV from the shader-visible heap and index into the per-draw payload.
  StructuredBuffer<PerDrawOverrides> overrides_buf =
    ResourceDescriptorHeap[meta.instance_metadata_buffer_index];

  return overrides_buf[meta.instance_metadata_offset].base_color_tint;
}

// Example usage at shading time:
// MaterialConstants m = material_buf[meta.material_handle];
// float4 tint = FetchBaseColorTint(meta);
// float4 shaded_base_color = m.base_color * tint;
```

**UV override policy (final):**

- Per-draw UV overrides are not part of this renderer contract.
- UV transform comes from `MaterialConstants` only.

### 2.4 MaterialConstants (StructuredBuffer, 80-byte stride)

**HLSL contract include:** defined in §5.

**ABI (must match `sizeof(MaterialConstants) == 80`):**

```hlsl
struct MaterialConstants
{
  float4 base_color;
  float  metalness;
  float  roughness;
  float  normal_scale;
  float  ambient_occlusion;
  uint   base_color_texture_index;
  uint   normal_texture_index;
  uint   metallic_texture_index;
  uint   roughness_texture_index;
  uint   ambient_occlusion_texture_index;
  uint   flags;
  float2 uv_scale;
  float2 uv_offset;
  uint   _pad0;
  uint   _pad1;
};
```

**Binding semantics:**

- `MaterialConstants` is provided as a *structured buffer SRV*.
- The bindless descriptor heap index for that SRV is `SceneConstants.bindless_material_constants_slot`.
- Shaders fetch a material snapshot via `material_handle` (from `DrawMetadata`) as the element index.
- Material “instances” are represented by additional resolved snapshots (additional elements) in this table; shaders do not require a separate instance concept.

**UV transform (required, not demo):**

- `uv_scale` / `uv_offset` are part of the final renderer contract and must remain supported.
- Default values are `uv_scale = (1,1)` and `uv_offset = (0,0)`.

**Texture index semantics (normative):**

- `*_texture_index` fields are absolute bindless texture indices.
- Index `0` selects the engine fallback texture for that slot (e.g., white/flat-normal). There is no “invalid texture index” in assets.

## 3) Shader request contract (human-readable, fully-specified)

This section defines the *final* API shape for requesting a shader module and the exact canonicalization rules for cache identity.

### 3.1 Final request type

**Final:** `ShaderStageDesc` becomes a readable request, not a pre-hashed string.

```cpp
// Strong types are required for critical indices/handles.
using DrawIndex = NamedType<uint32_t, struct DrawIndexTag,
  Hashable, Comparable, Printable>;

struct ShaderDefine {
  std::string name;      // validated, `[A-Z][A-Z0-9_]*`
  std::string value;     // empty => "-DNAME"; no whitespace
};

struct ShaderStageDesc {
  oxygen::ShaderType stage;       // Vertex/Pixel/Compute/etc.
  std::string source_path;        // normalized, relative to shader source root
  std::string entry_point;        // required, never implicit
  std::vector<ShaderDefine> defines; // empty allowed
};

// Root constants payload as a strongly-typed C++ value object.
struct RootConstants {
  DrawIndex draw_index;
  oxygen::ShaderVisibleIndex pass_constants_index; // oxygen::kInvalidShaderVisibleIndex if none
};
```

### 3.2 Canonicalization rules (no ambiguity)

The shader system must canonicalize requests before hashing/caching:

1. `source_path` (`ShaderSourcePath`):
   - must be relative
   - must be `lexically_normal()`
   - must use forward slashes
   - must not contain `..` after normalization
2. `entry_point`:
   - must be non-empty
   - ASCII identifier, validated (`[A-Za-z_][A-Za-z0-9_]*`)
3. `defines` (`name` / `value`):
   - names are validated (`[A-Z][A-Z0-9_]*`)
   - values are arbitrary ASCII without whitespace (empty allowed)
   - duplicated names are forbidden (fatal error)
   - defines are sorted lexicographically by `name` before hashing
4. Compiler environment:
   - shader model, backend, optimization/debug flags, `-HV`, and global defines are part of the cache identity

### 3.3 Human-readable logging key

For logging/debugging (not identity), the readable key format is:

`<STAGE>@<source_path>:<entry_point>`

In Oxygen, entry points follow the convention `VS`/`PS`/`CS` and many files contain one entry point per stage. In that common case, including both stage and entry point looks redundant. Both are kept intentionally:

- `stage` makes log lines easy to scan/grep and disambiguates when the same file contains multiple stages.
- `entry_point` remains part of the contract because the shader model and DXIL support multiple entry points (exports). Even if the engine convention usually makes `entry_point` equal to `stage`, the system still needs an explicit export name to bind the correct function.

Examples:

- `VS@Passes/Forward/ForwardMesh.hlsl:VS`
- `PS@Passes/Forward/ForwardMesh.hlsl:PS`
- `CS@Passes/Lighting/LightCulling.hlsl:CS`

## 4) Shader compilation & library contract (single source of truth)

### 4.1 Hard rule: runtime never compiles shaders

The engine runtime does not invoke DXC.

- DXC is a build-time tool concern.
- The runtime only loads a single shader library file for the active backend.

### 4.2 Library location (configurable, no hardcoded strings)

The shader library file location is provided by a core path-resolution service.

**Final source of truth (Config module):**

- `src/Oxygen/Config/IPathFinder.h` (interface)
- `src/Oxygen/Config/PathFinderConfig.h` (configuration)

**Hard rule:** `PlatformServices` is an internal concern of the graphics loader.
No other subsystem (engine/renderer/platform/graphics) may reference it directly.

**Boundary rule (normative):** configuration objects that cross module/DLL
boundaries MUST be 100% serializable. Therefore, `IPathFinder` instances (e.g.
`std::shared_ptr<IPathFinder>`) MUST NOT appear in subsystem config objects.

**Final ownership model (normative):**

- The *application* owns a single immutable `PathFinderConfig` value (the only
  shared thing).
- Each module/subsystem constructs its own `PathFinder`/`IPathFinder`
  implementation from that `PathFinderConfig` as needed.
- The graphics backend DLL constructs its own `PathFinder` instance from the
  serialized `PathFinderConfig` it receives during backend creation.

This makes path resolution deterministic without attempting to serialize or
share service pointers.

**Immutability requirement (C++20, normative):**

- Once created, a `PathFinderConfig` instance MUST be treated as immutable.
- Preferred enforcement pattern:
  - Represent it as an immutable value type: private data members, no setters,
    only `const` getters.
  - When sharing across long-lived subsystems, hold it via
    `std::shared_ptr<const PathFinderConfig>` (const-qualified pointee) to
    enforce read-only access at the type level.
  - Construct configs via a factory/builder function that returns a
    fully-initialized config (no partially-initialized states).

**Resolution rules (normative):**

- `PathFinderConfig::shader_library_path` is a path string (absolute or relative).
- If `shader_library_path` is absolute, the runtime uses it as-is.
- If `shader_library_path` is relative, `PathFinder` resolves it against the
  configured workspace root.
- If the workspace root is empty/unset, `PathFinder` resolves relative paths
  against the process working directory (the mechanism is implementation-defined
  and encapsulated by the graphics loader / platform layer).

**Final default path (repo-local layout):**

`bin/Oxygen/shaders.bin`

### 4.3 Library file format (shaders.bin)

This is a strict binary format.

**Header:**

- magic: `"OXSL"` (uint32)
- version: `2` (uint32)
- backend: fixed-size string `"d3d12"` (8 bytes, zero-padded)
- toolchain_hash: uint64 (hash of DXC version + fixed arguments schema)
- module_count: uint32

**Module table (module_count entries):**

Each module entry stores:

- stage (uint8)
- source_path (UTF-8, length-prefixed)
- entry_point (UTF-8, length-prefixed)
- define_count (uint16)
- defines: repeated `{name,value}` UTF-8 pairs (sorted by name, stored sorted)
- dxil_offset (uint64)
- dxil_size (uint64)
- reflection_offset (uint64)
- reflection_size (uint64)

**Payload blobs:**

- DXIL blob is stored as raw bytes.
- Reflection blob is stored as a compact, backend-specific binary payload.

### 4.4 Reflection minimum contract

Reflection is a required validation payload. The runtime uses it to reject shader
modules that violate Oxygen’s binding ABI and bindless rules.

The build pipeline produces reflection from DXC for every module in
`shaders.bin`.

**Stored reflection fields (per module, required):**

- `stage` (single stage)
- `entry_point` (export name)
- `shader_model_major`, `shader_model_minor`
- `numthreads_x`, `numthreads_y`, `numthreads_z` (zero for non-compute stages)
- Declared register bindings:
  - list of CBVs with `{register, space, byte_size}`
  - list of SRVs with `{register, space}`
  - list of UAVs with `{register, space}`
  - list of samplers with `{register, space}`

**Runtime validation rules (D3D12, final):**

1. `stage` and `entry_point` in reflection match the module table entry.
2. `numthreads_*` are validated when `stage == CS`:
   - `numthreads_x > 0`, `numthreads_y > 0`, `numthreads_z > 0`.
   - When `stage != CS`, all `numthreads_*` are `0`.
3. The only register-bound constant buffers are the engine contracts:
   - `SceneConstants` at `b1, space0` with `byte_size == sizeof(oxygen::engine::SceneConstants::GpuData)`.
   - `RootConstants` at `b2, space0` with `byte_size == (2 * sizeof(uint32_t))`.
4. There are zero additional register-bound CBVs.
5. There are zero register-bound SRVs.
6. There are zero register-bound UAVs.
7. There are zero register-bound samplers.

Any validation failure aborts library loading.

## 5) Shader source locations, layout, and include rules (final)

This section is the single authoritative source of truth for shader file
locations, include roots, and include strings.

### 5.1 Shader source locations (exclusive)

Shader sources exist in exactly two places:

1. Core module bindless HLSL (source generated and checked in):
   - `src/Oxygen/Core/Bindless`
2. Direct3D12 module shader sources (entry shaders and renderer contracts):
   - `src/Oxygen/Graphics/Direct3D12/Shaders`

Bindless HLSL files in the Core module MUST NOT be moved, copied, or wrapped.

### 5.2 Workspace root and include roots (final)

The workspace root is configured via `PathFinderConfig` and resolved by
`PathFinder`.

**Source of truth:**

- `src/Oxygen/Config/IPathFinder.h`
- `src/Oxygen/Config/PathFinderConfig.h`

If the configured workspace root is empty/unset, the workspace root is the
process working directory (resolved internally by `IPathFinder`).

Shader compilation MUST set these include roots:

1. `<workspace_root>/src/Oxygen`
2. `<workspace_root>/src/Oxygen/Graphics/Direct3D12/Shaders`

### 5.3 Required files (final)

**Core module bindless (must exist, used as-is):**

- `src/Oxygen/Core/Bindless/BindlessHelpers.hlsl`
- `src/Oxygen/Core/Bindless/Generated.BindlessLayout.hlsl`

**Direct3D12 module renderer contracts (must exist):**

- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/SceneConstants.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/DrawMetadata.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/MaterialConstants.hlsli`

**Direct3D12 module entry shaders (final layout):**

- `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardMesh.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Depth/DepthPrePass.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Lighting/LightCulling.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Ui/ImGui.hlsl`

### 5.4 Include rules and canonical include strings (final)

1. Entry shaders must not define engine contracts; they only include them.
2. Entry points are explicit. **Convention:** use `VS`/`PS`/`CS` for entry point
  names. No implicit `main`.
3. Include strings inside Direct3D12 shaders MUST NOT repeat the project name.
   The canonical include strings are:
   - `#include "Renderer/SceneConstants.hlsli"`
   - `#include "Renderer/DrawMetadata.hlsli"`
   - `#include "Renderer/MaterialConstants.hlsli"`
   - `#include "Core/Bindless/BindlessHelpers.hlsl"`

## 6) Light culling rewrite contract (Forward+ readiness)

`Passes/Lighting/LightCulling.hlsl` is a rewrite, not an edit.

**Mandatory ABI rules:**

- Must include the SceneConstants contract include (see §5.4) and bind `SceneConstants` at `b1, space0`.
- Must use `g_PassConstantsIndex` to fetch its pass constants CBV from `ResourceDescriptorHeap`.
- Must not declare an alternative SceneConstants and must not declare its own bindless indexing scheme.

## 7) Implementation plan (ordered checklist)

This checklist is intentionally grounded in the current Oxygen.Engine codebase.
Each step calls out *where the current behavior lives* and what must change to
reach the final spec in this document.

1. [X] Introduce `PathFinderConfig` + `PathFinder` construction API in the Config module.
   - Target API:
     - `src/Oxygen/Config/PathFinderConfig.h` (serializable)
     - `src/Oxygen/Config/IPathFinder.h` (optional interface; implementations are module-local)
   - Migration requirement:
     - Move `workspace_root_path` / `shader_library_path` out of `src/Oxygen/Config/RendererConfig.h` into `PathFinderConfig`.
     - Ensure all configs that must cross DLL boundaries carry only `PathFinderConfig` (or its serialized form), never `std::shared_ptr<IPathFinder>`.
     - Decide the internal construction entry point (e.g. `MakePathFinder(PathFinderConfig)`), usable from both the main executable and the backend DLL.

2. [X] **Eliminate hard-coded workspace roots and unify path resolution.**
   - Current hard-coded behavior:
     - `src/Oxygen/Graphics/Common/ShaderManager.cpp`: `GetWorkspaceRoot()` returns a *hard-coded* local path (`"F:/projects/DroidNet/projects/Oxygen.Engine/"`) for non-CI runs and uses `std::filesystem::current_path()` as a fallback in CI.
     - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.cpp`: hard-codes `.archive_dir = R"(bin\Oxygen)"` and `.source_dir = R"(src\Oxygen\Graphics\Direct3D12\Shaders)"` with a TODO comment.
   - Required refactor:
     - Remove *all* workspace-root guessing and hard-coded paths from `ShaderManager` and the D3D12 shader catalog.
     - Use `PathFinderConfig` as the single configurable input and a locally-constructed `PathFinder` as the single way to:
       - resolve workspace root
       - resolve the shader library path
       - derive shader include roots
     - Ensure no code outside the loader references `PlatformServices` for path resolution.
     - Plumb `PathFinderConfig` through the graphics/renderer initialization entry points so shader compilation/build tooling (offline) and runtime loading share the same canonical path rules.

3. [X] **Inventory and pin down all current shader system entry points and call sites.**
   - Runtime shader compilation + caching currently flows through:
     - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.{h,cpp}` (constructs a `ShaderManager` and exposes `EngineShaders::GetShader(unique_id)`)
     - `src/Oxygen/Graphics/Common/ShaderManager.{h,cpp}` (builds/loads/saves the `OXSH` archive and recompiles when sources change)
     - `src/Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.{h,cpp}` + `src/Oxygen/Graphics/Common/ShaderCompiler.{h,cpp}` (DXC compile + shared compile plumbing)
   - Runtime bytecode fetch entry points (D3D12):
     - `src/Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.cpp`:
       - PSO creation loads bytecode via `gfx->GetShader(desc.shader)`.
       - `graphics::ShaderStageDesc::entry_point_name` is currently *not* used here; it only affects pipeline hashing (`src/Oxygen/Graphics/Common/PipelineState.cpp`).
     - `src/Oxygen/Graphics/Direct3D12/Graphics.cpp`:
       - `Graphics::GetShader(unique_id)` delegates to the D3D12 `EngineShaders` instance.
   - Runtime bytecode fetch entry points (Headless):
     - `src/Oxygen/Graphics/Headless/Internal/EngineShaders.cpp`:
       - `EngineShaders::GetShader(unique_id)` generates deterministic fake “bytecode” keyed by the unique-id string.
       - Pre-warms a small fixed set of shader IDs (hard-coded `"VS@..."`/`"PS@..."`/`"CS@..."` literals).
   - Pipeline call sites currently reference shaders via string IDs built by:
     - `src/Oxygen/Graphics/Common/Shaders.{h,cpp}` (`MakeShaderIdentifier(ShaderType, relative_path)` produces `"<STAGE>@<path>"`)
     - Used in render passes:
       - `src/Oxygen/Renderer/Passes/DepthPrePass.cpp`
       - `src/Oxygen/Renderer/Passes/ShaderPass.cpp`
       - `src/Oxygen/Renderer/Passes/TransparentPass.cpp`
     - Also used internally by the shader system:
       - `src/Oxygen/Graphics/Common/ShaderManager.cpp` (uses `MakeShaderIdentifier(profile)` as a cache key / archive unique-id)
       - `src/Oxygen/Graphics/Common/ShaderCompiler.cpp` (formats identifiers for error messages)
   - Data-level unique-id coupling (PAK/material pipeline):
     - `src/Oxygen/Data/PakFormat.h` (`ShaderReferenceDesc::shader_unique_id` stores `"<STAGE>@<path>"`; stage inferred from prefix before `@`)
     - `src/Oxygen/Data/ShaderReference.cpp` (documents the same assumption; references `MakeShaderIdentifier()`)
     - Tests that encode/assume the same format:
       - `src/Oxygen/Data/Test/MaterialAsset_test.cpp`
       - `src/Oxygen/Content/Test/MaterialLoader_test.cpp`

4. [X] **Update bindless root-constants ABI (b2, space0) to the final 2×32-bit layout.**
   - Current ABI (implemented):
     - `src/Oxygen/Core/Meta/Bindless.yaml` defines `RootConstants` (root param index 3, `b2, space0`) with `num_32bit_values: 2`.
     - `src/Oxygen/Core/Bindless/Generated.RootSignature.h` declares `RootParam::kRootConstants` with `kRootConstantsConstantsCount = 2`.
     - `src/Oxygen/Renderer/Passes/RenderPass.cpp`:
       - Binds `g_PassConstantsIndex` (offset 1) once per pass to `oxygen::kInvalidBindlessIndex`.
       - Binds `g_DrawIndex` (offset 0) per draw via `BindDrawIndexConstant()`.
     - HLSL entry shaders now declare `cbuffer RootConstants : register(b2, space0)` with:
       - `uint g_DrawIndex;`
       - `uint g_PassConstantsIndex;`

5. [X] **Refactor RenderPass binding API to match the new root constants contract.**
   - Current:
     - `src/Oxygen/Renderer/Passes/RenderPass.{h,cpp}` binds `g_DrawIndex` per draw and `g_PassConstantsIndex` once per pass, but previously used raw integers.
   - Target:
     - Provide pass-level binding for `g_PassConstantsIndex` (set once per pass/dispatch) and draw-level binding for `g_DrawIndex`.
     - Introduce/standardize the strong types required by the spec:
       - `DrawIndex` (strong type over `uint32_t`)
       - `oxygen::ShaderVisibleIndex` for `g_PassConstantsIndex` (using `oxygen::kInvalidShaderVisibleIndex` sentinel).
   - Implemented:
     - `src/Oxygen/Renderer/Types/DrawIndex.h` introduces `oxygen::engine::DrawIndex`.
     - `src/Oxygen/Renderer/Passes/RenderPass.{h,cpp}` now:
       - uses `DrawIndex` for `g_DrawIndex` binding.
       - uses `oxygen::ShaderVisibleIndex` for `g_PassConstantsIndex` binding.
       - defaults `g_PassConstantsIndex` to `oxygen::kInvalidShaderVisibleIndex` and exposes `SetPassConstantsIndex()`.
   - Scope:
     - `src/Oxygen/Renderer/Passes/RenderPass.{h,cpp}` and every pass that issues draws/dispatches.
     - Any `CommandRecorder` helpers needed to set *multiple* 32-bit root constants in one call.

6. [X] **Replace shader stage descriptors in pipeline descriptions with the final request shape.**
   - Current API surface:
     - `src/Oxygen/Graphics/Common/PipelineState.h`: `graphics::ShaderStageDesc` is `{ std::string shader; std::optional<std::string> entry_point_name; }` where `shader` is a string ID.
     - That string ID is currently produced by `MakeShaderIdentifier()` which does *not* include entry point or defines.
   - Target API surface (per §3):
     - Replace `graphics::ShaderStageDesc` with a request that carries:
       - stage (`oxygen::ShaderType`)
       - `source_path` (normalized, forward slashes, repo-relative)
       - `entry_point` (required)
       - per-request `defines` (validated/sorted)
   - Scope:
     - Pipeline desc hashing: `src/Oxygen/Graphics/Common/PipelineState.cpp` currently hashes only the `shader` string and optional entry point.
     - All PSO builders and tests that construct shader stages (see `src/Oxygen/Graphics/*/Test/*PipelineState*`).

7. [X] **Remove `MakeShaderIdentifier()` from cache identity and migrate all call sites.**
   - Current identity coupling:
     - `src/Oxygen/Graphics/Common/Shaders.{h,cpp}` defines `MakeShaderIdentifier()`.
     - `src/Oxygen/Data/ShaderReference.cpp` assumes the shader type can be inferred from the identifier format.
     - Render passes call `MakeShaderIdentifier()` directly when creating PSOs.
   - Target:
     - Identity is derived only from canonicalized requests (source path + entry point + defines + compiler environment).
     - Keep a *log-only* helper that formats `<STAGE>@<source_path>:<entry_point>`.
     - Delete or repurpose `ShaderReference`’s unique-id assumptions.

8. [X] **Implement strict canonicalization + validation for shader requests and use it as the only key.**
   - Target rules are §3.2 (relative normalized paths, identifier validation, define validation, duplicate forbid, sorted defines).
   - Scope:
     - New canonicalization component (location TBD in code, but used by both the offline builder and runtime loader).
     - New unit tests in an existing test suite (e.g. under `src/Oxygen/Graphics/Common/Test/`).

9. [X] **Extend the compiler options and DXC argument emission to support request defines.**
   - Current gaps:
     - `src/Oxygen/Graphics/Common/ShaderCompiler.h`: `ShaderCompileOptions` only carries `include_dirs`.
     - `src/Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.cpp`: has commented-out code for adding `-D` defines; currently emits none.
   - Target:
     - Add per-request defines to `ShaderCompileOptions` and emit DXC `-D` for *global defines* (`ShaderCompiler::Config::global_defines`) plus request defines.
     - Ensure DXC include roots match §5.2 exactly:
       - `<workspace_root>/src/Oxygen`
       - `<workspace_root>/src/Oxygen/Graphics/Direct3D12/Shaders`
   - Implemented:
     - `src/Oxygen/Graphics/Common/ShaderCompiler.h`: `ShaderCompileOptions` now carries `defines`.
     - `src/Oxygen/Graphics/Common/ShaderManager.cpp`: plumbs request `defines` into compile options.
     - `src/Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.cpp`: emits DXC `-D` for global defines, then request defines (request takes precedence).

10. [X] **Create the renderer contract HLSL includes and remove contract redeclarations from entry shaders.**
    - Current state (violates §2 and §5.4):
      - `src/Oxygen/Graphics/Direct3D12/Shaders/DepthPrePass.hlsl` and `FullScreenTriangle.hlsl` re-declare `SceneConstants`, `DrawMetadata`, and (for fullscreen) `MaterialConstants`.
    - Target:
      - Add the contract includes in `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/`:
        - `SceneConstants.hlsli`, `DrawMetadata.hlsli`, `MaterialConstants.hlsli`
      - Update all entry shaders to include:
        - `#include "Renderer/SceneConstants.hlsli"`
        - `#include "Renderer/DrawMetadata.hlsli"`
        - `#include "Renderer/MaterialConstants.hlsli"`
        - `#include "Core/Bindless/BindlessHelpers.hlsl"`
      - Delete duplicated struct declarations from entry shaders.

11. [X] **Restructure D3D12 shader sources into the final pass-oriented layout and update references.**
    - Current layout (flat): `src/Oxygen/Graphics/Direct3D12/Shaders/*.hlsl` (`DepthPrePass.hlsl`, `FullScreenTriangle.hlsl`, `LightCulling.hlsl`, `ImGui.hlsl`).
    - Target layout (per §5.3): `Passes/<PassName>/<File>.hlsl`.
    - Scope:
      - Update the “engine shader catalog” currently defined in `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.cpp` (`kEngineShaders` list).
      - Update all render-pass PSO creation call sites that currently pass flat filenames.
      - Update shader compilation inputs (see step 14) to discover/pass the new paths.

12. [ ] **Rewrite LightCulling to comply with bindless-only rules and RootConstants.**
    - Current shader is incompatible with this spec:
      - `src/Oxygen/Graphics/Direct3D12/Shaders/LightCulling.hlsl` declares a separate `cbuffer ResourceIndices : register(b0)` and uses register-bound SRVs/UAVs in explicit spaces.
    - Target:
      - Replace it with `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Lighting/LightCulling.hlsl` that:
        - Binds `SceneConstants` at `b1, space0` (contract include)
        - Uses `g_PassConstantsIndex` to fetch its pass constants CBV from `ResourceDescriptorHeap`
        - Declares **zero** register-bound SRVs/UAVs/samplers

13. [ ] **Resolve ImGui’s current root-constant usage against the new ABI.**
    - Current:
      - `src/Oxygen/Graphics/Direct3D12/Shaders/ImGui.hlsl` uses `cbuffer ImGuiPackedIndices : register(b2)` for a packed texture/sampler index.
    - Target constraint:
      - `b2, space0` is reserved for the engine’s fixed two 32-bit root constants.
    - Required work:
      - Move per-draw UI texture selection out of ad-hoc root constants and into the standardized mechanism (either via `DrawMetadata` + `g_DrawIndex`, or via a UI-specific per-draw indirection buffer referenced by pass constants).
      - Update the D3D12 ImGui integration code that sets root parameters (likely under `src/Oxygen/Graphics/Direct3D12/ImGui/`) to bind root constants per the new ABI.

14. [ ] **Replace the existing runtime “shader archive” with the final `shaders.bin` format and a build-time producer.**
    - Current archive system is not the spec format:
      - `src/Oxygen/Graphics/Common/ShaderManager.cpp` writes `kArchiveMagic = "OXSH"`, `kArchiveVersion = 1` and stores bytecode + source hashes; it also recompiles at runtime.
    - Target:
      - Implement the §4.3 `OXSL` / version 2 `shaders.bin` format with module table + DXIL + reflection payload.
      - Implement an offline build step that produces `bin/Oxygen/shaders.bin`.
    - Scope:
      - Rework the build pipeline currently in `src/Oxygen/Graphics/Direct3D12/Shaders/CMakeLists.txt` (which today compiles individual `.cso` files) so the build produces *one* authoritative artifact.

15. [ ] **Implement the runtime shader library loader + reflection validation gates.**
    - Target behavior (hard rule): runtime never invokes DXC.
    - Scope:
      - Replace `EngineShaders` + `ShaderManager` runtime compilation flow with a loader that:
        - Resolves the shader library path via `PathFinderConfig`/`PathFinder` per §4.2
        - Loads `shaders.bin`
        - Validates reflection per §4.4 (ABI + bindless-only discipline)
        - Provides lookups by canonical request identity

16. [ ] **Delete runtime shader compilation and all dual-path shader loading.**
    - Remove/retire:
      - Runtime recompilation/update logic in `src/Oxygen/Graphics/Common/ShaderManager.{h,cpp}`.
      - Hard-coded workspace-root logic (`GetWorkspaceRoot()`), and any code paths that depend on local filesystem shader sources at runtime.
    - Ensure the engine loads shaders *exclusively* from the shader library path resolved by `PathFinderConfig`/`PathFinder`.

17. [ ] **Add a CI/build gate for shader discipline violations.**
    - Enforce (at minimum):
      - no contract redeclarations in entry shaders (only includes)
      - includes only from the two allowed source locations (§5.1) and only via canonical include strings (§5.4)
      - zero register-bound SRVs/UAVs/samplers (bindless-only)
    - Implementation options grounded in current code:
      - Extend the offline builder to fail the build when reflection validation fails.
      - Optionally reuse/replace the include-scanning logic already present in `src/Oxygen/Graphics/Common/ShaderManager.cpp` (`ExtractQuotedIncludes` / `ResolveIncludePath`) as a build-time linter.

18. [ ] **Add unit tests that lock down the refactor’s invariants.**
    - Canonicalization: paths, entry points, defines (sorting + duplicate rejection).
    - Workspace root / library path resolution: `IPathFinder` behavior (including “empty means working dir”).
    - Library validation: rejection of illegal bindings (e.g. the current `LightCulling.hlsl` pattern should be a known failing case).
