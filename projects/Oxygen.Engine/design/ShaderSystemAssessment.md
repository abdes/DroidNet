# Oxygen Engine Shader System — Structure & Design Assessment

Date: 2026-01-04

## Executive summary

The current shader system **works as a prototype**, but it is not structurally ready for a complete material system + lighting integration. The key issues (and the updated constraints for the next iteration) are:

1. **Two competing shader build paths** exist (CMake → `.cso` vs runtime `ShaderManager` → `shaders.bin`) with no single source of truth.
2. **Shader keys are underspecified for scalability**, but they must remain **human-readable at render-pass PSO call sites** (the render code should not traffic in opaque hashes). Internally, the shader system may hash after parsing a readable key.
3. **Shader include/define discipline is not formalized**, leading to duplicated struct layouts and inevitable drift.
4. **`FullScreenTriangle.hlsl` is misnamed and monolithic**: it is the main bindless mesh forward shading shader today and must be renamed/restructured so it can evolve into a material + lighting pipeline.
5. **`LightCulling.hlsl` must be completely rewritten**. The current file carries its own binding/constant contract and cannot be “patched into correctness” without becoming another divergent fork.

The good news: Oxygen already has the right primitives on the CPU side (`engine::SceneConstants`, `engine::DrawMetadata`, `engine::MaterialConstants`) and a clear bindless data path via descriptor slots carried in `SceneConstants`. The next step is to make HLSL consume those primitives via shared includes and to make pipeline creation pass fully-specified, readable shader requests.

## What exists today (inventory)

### Runtime system (what the engine actually uses)

- **`ShaderCompiler` (D3D12)**: DXC wrapper compiles from source string.
  - Files: `src/Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.h`, `.../ShaderCompiler.cpp`
  - Uses SM 6.6 profiles, `-HV 2021`, debug flags in Debug builds.
  - Has scaffolding for defines but it’s **commented out**.

- **`ShaderManager` (common)**: loads/saves a custom `shaders.bin` archive and recompiles shaders when “outdated”.
  - Files: `src/Oxygen/Graphics/Common/ShaderManager.h`, `.../ShaderManager.cpp`
  - Outdated detection: hash of file contents + quoted includes + timestamp.
  - **Hard-coded workspace root** fallback in non-CI builds.

- **Engine shader pre-warm**:
  - D3D12: `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.cpp`
  - Headless: `src/Oxygen/Graphics/Headless/Internal/EngineShaders.cpp`

- **Pipeline creation** requests shaders by string ID:
  - `PipelineStateCache` calls `Graphics::GetShader(desc.shader)`.
  - File: `src/Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.cpp`

### CPU-side contracts already present (and should be authoritative)

These types already exist and already document size/alignment expectations; they should be the “truth” that HLSL consumes via shared includes:

- `oxygen::engine::SceneConstants::GpuData` in `src/Oxygen/Renderer/Types/SceneConstants.h`.
  - Carries the dynamic bindless descriptor slots used by shaders (draw metadata, transforms, normal matrices, material constants).
- `oxygen::engine::DrawMetadata` in `src/Oxygen/Renderer/Types/DrawMetadata.h`.
  - Has a size assert and is used per-draw by render passes.
- `oxygen::engine::MaterialConstants` in `src/Oxygen/Renderer/Types/MaterialConstants.h`.
  - Already defines the material snapshot shape used by `MaterialBinder`.

### Build-time shader compilation (currently separate)

- CMake compiles `.hlsl` → `.cso` into `${build}/shaders/$<CONFIG>`.
  - File: `src/Oxygen/Graphics/Direct3D12/Shaders/CMakeLists.txt`
  - This path appears **not used by the runtime shader archive**.

### HLSL set

- `FullScreenTriangle.hlsl` — despite the name, it is the main bindless mesh VS + a forward shading PS.
- `DepthPrePass.hlsl` — depth-only.
- `LightCulling.hlsl` — Forward+ tile culling compute prototype; to be replaced.
- `ImGui.hlsl` — UI-only.
- `MaterialFlags.hlsli` — currently a single flag.

## Findings (specific, actionable)

### 1) Two compilation pipelines are fighting each other

**Where:**

- Build-time DXC: `src/Oxygen/Graphics/Direct3D12/Shaders/CMakeLists.txt`
- Runtime DXC + archive: `src/Oxygen/Graphics/Common/ShaderManager.cpp`, `src/Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.cpp`

**Why it matters:**

- You pay build time to produce `.cso` files, but the engine runtime path compiles from source anyway.
- Options differ (PDB handling, `-Ges`, `-Qembed_debug` vs `-Fd`), producing different outputs and different debugging experiences.

**Action:** pick one source of truth:

- **Shipping / CI**: offline compile → packaged shader library.
- **Dev**: hot reload by compiling from source, but still feeding the same shader library abstraction.

Concrete change:

- Introduce a single “shader artifact” format (see proposal below) and make both CMake and runtime output/consume that format.

### 2) Shader keys must stay human-readable at PSO call sites

**Where:**

- `ShaderStageDesc` in `src/Oxygen/Graphics/Common/PipelineState.h` stores a `shader` string and an optional `entry_point_name`.
- `MakeShaderIdentifier()` in `src/Oxygen/Graphics/Common/Shaders.cpp` currently produces `"VS@Path"`, `"PS@Path"`, etc.

**Problem:**

- For materials + lighting, the shader request must include at least: shader file, stage, entry point, and a set of defines that represent the selected material/feature path.
- Oxygen’s render passes must be able to read pipeline descriptions in code and understand exactly what shader variant is being requested. Opaque hashes embedded into `ShaderStageDesc.shader` break that.

**Action:** split responsibilities explicitly:

- **Human-readable request at the PSO call site**
  - `ShaderStageDesc.shader` remains a readable logical ID that always includes the stage + normalized source path (current `MakeShaderIdentifier()` shape is compatible with this).
  - `ShaderStageDesc.entry_point_name` is always set explicitly when the file has multiple entry points.
  - Add explicit define support to `ShaderStageDesc` (new field) rather than encoding defines into an unreadable string.

- **Internal cache key derived from the readable request**
  - The shader system parses the readable request (stage, source path, entry point, defines, compiler flags, backend) into an internal key.
  - That internal key may be hashed for performance and map lookup.

**Concrete spec (grounded in existing Oxygen types):**

- `ShaderStageDesc` should become:
  - `shader`: stage + normalized relative shader path (existing behavior)
  - `entry_point_name`: required when not `"main"`
  - `defines`: required when a pass/material toggles compile-time codepaths

This keeps render passes readable while still allowing `ShaderManager` to use strong internal identity.

### 3) Binding contracts must be centralized via includes (HLSL must not redefine engine structs)

**Where:**

- Engine “truth” for `SceneConstants`: `src/Oxygen/Renderer/Types/SceneConstants.h`
- Engine “truth” for `DrawMetadata`: `src/Oxygen/Renderer/Types/DrawMetadata.h`
- Engine “truth” for `MaterialConstants`: `src/Oxygen/Renderer/Types/MaterialConstants.h`
- Current HLSL duplicates these layouts inside `FullScreenTriangle.hlsl` and defines a divergent `SceneConstants` in `LightCulling.hlsl`.

**Impact:**

- Any HLSL struct duplication is a latent bug: it is guaranteed to drift as Oxygen evolves `SceneConstants` (bindless slots, frame tracking) and expands material data.

**Action (must-do before integrating lighting):**

Create HLSL includes that mirror the existing C++ types and require all shaders to consume them:

- `Shaders/Oxygen/Renderer/SceneConstants.hlsli`
  - Mirrors `oxygen::engine::SceneConstants::GpuData` exactly.
  - Declares `cbuffer SceneConstants : register(b1)`.
- `Shaders/Oxygen/Renderer/DrawMetadata.hlsli`
  - Mirrors `oxygen::engine::DrawMetadata` exactly.
  - Declares the `DrawMetadata` struct and any shared bit definitions used by render passes.
- `Shaders/Oxygen/Renderer/MaterialConstants.hlsli`
  - Mirrors `oxygen::engine::MaterialConstants` exactly.
  - Defines how materials are fetched in shaders using `SceneConstants.bindless_material_constants_slot`.

Once these exist, `FullScreenTriangle.hlsl` and the replacement for `LightCulling.hlsl` must not declare their own versions of these structs.

### 4) Shader archive format is not future-proof

**Where:**

- `ShaderManager::Save()` / `Load()` in `src/Oxygen/Graphics/Common/ShaderManager.cpp`

Issues to address soon:

- It stores bytecode as `std::vector<uint32_t>`; that assumes alignment/size divisibility by 4 and is brittle for “byte blob” formats.
- It stores `source_file_path` (string) as whatever `std::filesystem` produced; likely becomes absolute and machine-specific.
- No metadata is stored (reflection, resources used, threadgroup size, etc.).
- Cache invalidation ignores compile flags/defines.

**Action:** evolve the archive into a “shader library” concept:

- Store **raw bytes**.
- Store a **normalized** `source_path` relative to a configured root.
- Store readable request components (stage, path, entry point, defines, compiler flags) and a `compile_environment_hash`.
- Store reflection metadata needed by runtime validation.

### 5) `FullScreenTriangle.hlsl` must be renamed and split along Oxygen concepts

**Where:**

- `src/Oxygen/Graphics/Direct3D12/Shaders/FullScreenTriangle.hlsl`

Findings:

- The file mixes:
  - bindless mesh vertex fetch,
  - `DrawMetadata` decoding,
  - transform fetch via `SceneConstants.bindless_transforms_slot`,
  - material fetch via `SceneConstants.bindless_material_constants_slot`,
  - forward shading.
- Name suggests a fullscreen helper, but it is used by `ShaderPass` and `TransparentPass` for mesh rendering.

**Action:** rename and restructure to make the intent explicit and to align with Oxygen’s existing CPU-side types.

- **Rename the entry file**
  - Replace `FullScreenTriangle.hlsl` with `Passes/Forward/ForwardMesh.hlsl`.
  - Keep the same stage entry points currently used by Oxygen passes (vertex + pixel).

- **Split shared engine contracts into includes**
  - Move duplicated `SceneConstants`, `DrawMetadata`, and `MaterialConstants` HLSL declarations into the dedicated includes described in Finding 3.

- **Split bindless helpers out of the entry shader**
  - Create `Shaders/Oxygen/Bindless/DescriptorHeap.hlsli` to own all `ResourceDescriptorHeap`-based typed access helpers.
  - Enforce that all resource access is funneled through these helpers so slot conventions remain centralized.

This makes the forward mesh pass a thin adapter layer over stable engine contracts instead of a monolith.

### 6) Material + lighting scaling requires fully specified shader requests

**Where:**

- Current usage passes a shader ID string into pipeline descriptions:
  - `ShaderPass` / `DepthPrePass` create `GraphicsPipelineDesc` with `ShaderStageDesc{ .shader = "VS@Path" }`.

**Why it matters in Oxygen specifically:**

- Oxygen already separates “pipeline description” (`GraphicsPipelineDesc` / `ComputePipelineDesc`) from “shader loading” (`PipelineStateCache` → `Graphics::GetShader()`). The missing piece is that the *request* reaching `GetShader()` is not sufficiently specified (defines/configuration), and the render code has no readable way to express variants.

**Action:** evolve `ShaderStageDesc` so render passes can supply the full request in a readable way:

- `shader`: still a readable stage + path (what a human recognizes in the pass)
- `entry_point_name`: explicit when needed
- `defines`: explicit list for feature selection

This keeps pass code readable and makes it possible for `ShaderManager` to build a robust internal cache key without leaking hashes into render code.

## Proposed restructuring (recommended target design)

### A) Unify around a single shader asset pipeline

**Goal:** one path that supports both dev iteration and shipping builds.

**Components:**

- `ShaderSourceRegistry`
  - Owns shader roots and resolves logical shader paths.
  - Enforces normalized paths (forward slashes, relative paths).

- `ShaderCompiler` (backend-specific)
  - Compiles a fully specified shader request.
  - Applies global defines + per-request defines.
  - Produces a shader artifact (bytecode + reflection + diagnostics).

- `ShaderLibrary` (runtime)
  - Loads precompiled artifacts from disk (packaged in build).

- Offline compilation remains a build concern
  - Oxygen’s CMake shader compilation and the runtime `ShaderManager` must converge on the same artifact layout (directory + file naming + compiler options).
  - The engine must be able to load the offline artifacts without needing runtime compilation.

**Immediate win:** removes the CMake/runtime duplication, gives consistent debugging, and makes it possible to ship without runtime compilation.

### B) Add reflection + validation gates

Even in a bindless engine, reflection is valuable for:

- ensuring a shader doesn’t accidentally require a non-existent root parameter,
- detecting register collisions,
- verifying constant buffer sizes and fields,
- recording threadgroup sizes for compute.

For D3D12/DXC you can extract reflection from DXIL container (or keep sideband JSON produced at compile time).

Suggested minimum metadata to store per `ShaderModule`:

- stage, entry, profile
- list of CBVs/SRVs/UAVs and their registers/spaces
- root constant usage
- for compute: `[numthreads(x,y,z)]`

### C) Defines and includes must be specified for scalability (Oxygen rules)

This is the core scalability requirement for “full material system + lighting” in Oxygen: shader source structure must ensure that contracts are shared and that any compile-time path is explicitly requested.

**Include rules (must be enforced by convention and CI build failures):**

- All includes are rooted within `src/Oxygen/Graphics/Direct3D12/Shaders`.
- No shader may re-declare `SceneConstants`, `DrawMetadata`, or `MaterialConstants` in an entry file.
- Shared contracts live under `Shaders/Oxygen/...` and are included by all passes.

**Define rules (must be supported end-to-end by ShaderCompiler + ShaderManager):**

- Global defines originate from the engine build configuration and backend:
  - `OXYGEN_SHADER_BACKEND_D3D12` (always defined for the D3D12 backend)
  - `OXYGEN_SHADER_DEBUG` (Debug builds)
- Feature defines originate from render pass configuration and material data:
  - Defines are the only acceptable mechanism for compile-time feature selection.
  - When the shader path depends on a feature, the render pass must specify that define via `ShaderStageDesc`.

This replaces the current state where defines are partially scaffolded but not wired, and where shaders embed TODO-driven branching without a stable mechanism to select variants.

### D) Light culling: rewrite (do not evolve the current `LightCulling.hlsl`)

The current `src/Oxygen/Graphics/Direct3D12/Shaders/LightCulling.hlsl` is a self-contained prototype that defines its own `SceneConstants` and resource indexing scheme. That approach is incompatible with Oxygen’s existing CPU-side contracts.

**Rewrite requirements (grounded in existing Oxygen contracts):**

- `cbuffer SceneConstants : register(b1)` must exactly match `oxygen::engine::SceneConstants::GpuData`.
- Bindless resource discovery must use the slots already carried by `SceneConstants` where applicable.
- Any additional data required by light culling (depth SRV slot, light buffer slot, output UAV slots, dispatch dimensions) must be carried in a pass-specific constant buffer distinct from `b1`.

**C++ side implications (Oxygen-specific):**

- If light culling requires additional view data (inverse projection, screen dimensions), Oxygen must add those fields to `SceneConstants::GpuData` (and its HLSL mirror include) so all passes share the same view contract.
- If light types do not exist yet in `src/Oxygen/Renderer/Types`, introduce them there and mirror them in `Shaders/Oxygen/Renderer` so the lighting contract is shared between CPU and HLSL.

### E) Fix portability and configuration

Critical cleanup items:

- Remove hard-coded root paths in `ShaderManager` and `EngineShaders`.
  - Use a configured project root (from executable location, config file, or CMake-defined constant).
- Avoid embedding absolute paths in archives.
- Ensure include directories are deterministic.

## Priority roadmap (actionable steps)

### Phase 0 (1–3 days): stop the bleeding

1. **Centralize engine HLSL contracts**
  Create `Shaders/Oxygen/Renderer/SceneConstants.hlsli` (mirrors `SceneConstants::GpuData`) and create `Shaders/Oxygen/Renderer/DrawMetadata.hlsli` / `Shaders/Oxygen/Renderer/MaterialConstants.hlsli` to remove duplicated struct declarations.

1. **Implement defines end-to-end**
  Wire `ShaderCompiler::Config::global_defines` (and per-request defines) into DXC arguments and update `ShaderManager` cache identity to include defines and compile flags.

1. **Remove hard-coded workspace root**
  Replace `GetWorkspaceRoot()` hardcode with a configured shader root derived from the executable/build layout.

### Phase 1 (1–2 weeks): establish the architecture

1. Evolve `ShaderStageDesc` to carry defines explicitly (human-readable at PSO sites).
1. Update `MakeShaderIdentifier()` usage so shader requests remain readable but fully specified.
1. Replace `FullScreenTriangle.hlsl` with `Passes/Forward/ForwardMesh.hlsl` and split shared contracts/bindless helpers into `.hlsli`.

### Phase 2 (2–6 weeks): materials + lighting-ready

1. Add reflection capture and store it in the library.
1. Add program-level caching where it improves PSO creation and validation.
1. Replace `LightCulling.hlsl` with the rewritten shader that consumes Oxygen contracts and produces a stable lighting output.

## Concrete file-level recommendations

- Replace `src/Oxygen/Graphics/Direct3D12/Shaders/FullScreenTriangle.hlsl` with `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardMesh.hlsl`.
- Add shared includes under `src/Oxygen/Graphics/Direct3D12/Shaders/Oxygen/Renderer` mirroring:
  - `src/Oxygen/Renderer/Types/SceneConstants.h`
  - `src/Oxygen/Renderer/Types/DrawMetadata.h`
  - `src/Oxygen/Renderer/Types/MaterialConstants.h`

- Keep `ImGui.hlsl` isolated (UI exception) but align its packed root constants with a documented contract.

- Keep `MakeShaderIdentifier()` readable (stage + normalized relative path). Do not turn it into a hash.
- Carry entry point via `ShaderStageDesc.entry_point_name` and carry defines via an explicit field on `ShaderStageDesc`.

- Evolve `ShaderManager`:
  - store bytecode as bytes,
  - store normalized relative source paths,
  - include compile environment hash.

## Notes on what is already good

- The bindless strategy and per-draw indirection (`DrawMetadata`, dynamic bindless slots) is a solid modern foundation.
- `PipelineStateCache` cleanly isolates PSO creation and provides a single choke point for shader loading.
- The engine already enforces 16-byte alignment and has a C++/HLSL size assert for `SceneConstants`, which is exactly the right discipline.

---

This document intentionally stays grounded in Oxygen’s current code (existing pass call sites, existing CPU types, and current shader files) and specifies concrete restructuring rules rather than hypothetical feature matrices.
