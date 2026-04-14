# SceneTextures Low-Level Design

**Phase:** 2 — SceneTextures and SceneRenderer Shell
**Deliverable:** D.1
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This System Is

`SceneTextures` is the canonical scene-product family for Vortex. It owns and
manages the shared GPU texture resources that flow across the frame: GBuffers,
scene depth, scene color, velocity, stencil, and custom depth. It is the
deferred renderer's central data product.

### 1.2 Why It Is Needed

The legacy Forward+ renderer has no unified scene-texture product family.
Deferred rendering requires a single authoritative owner of the GBuffer
attachments and related screen-space products that multiple stages and services
read and write during a frame.

### 1.3 What It Replaces

No direct legacy equivalent. The closest analog is the scattered per-pass
framebuffer setup in the legacy `ForwardPipeline`. Vortex consolidates all
shared scene attachments under one product family.

### 1.4 Architectural Authority

- [ARCHITECTURE.md §7.3](../ARCHITECTURE.md) — four-part scene-texture
  contract (authoritative)
- [ARCHITECTURE.md §7.3.2](../ARCHITECTURE.md) — canonical product family
- [ARCHITECTURE.md §7.3.3](../ARCHITECTURE.md) — setup mode rules
- [ARCHITECTURE.md §7.3.4](../ARCHITECTURE.md) — binding package rules

## 2. Four-Part Contract

Per ARCHITECTURE.md §7.3.1, `SceneTextures` has four separate architectural
concerns. This LLD designs each part.

| Contract Part | Class | Owner |
| ------------- | ----- | ----- |
| Concrete product family | `SceneTextures` | `SceneRenderer` |
| Setup state | `SceneTextureSetupMode` | `SceneRenderer` |
| Shader-facing binding package | `SceneTextureBindings` | Generated from SceneTextures + setup mode |
| Extracted handoff set | `SceneTextureExtracts` | `SceneRenderer` post-render cleanup |

## 3. Interface Contracts

### 3.1 SceneTexturesConfig

Configuration for initial allocation. Immutable after construction.

```cpp
namespace oxygen::vortex {

struct SceneTexturesConfig {
  glm::uvec2 extent{0, 0};                  // Viewport dimensions
  bool enable_velocity{true};                // SceneVelocity allocation
  bool enable_custom_depth{false};           // Separate custom depth/stencil path
  std::uint32_t gbuffer_count{4};            // A-D active; E-F reserved
  std::uint32_t msaa_sample_count{1};        // 1 = no MSAA
};

} // namespace oxygen::vortex
```

**File:** `SceneRenderer/SceneTextures.h`

### 3.2 GBufferIndex

Typed index vocabulary for GBuffer access.

```cpp
enum class GBufferIndex : std::uint8_t {
  kNormal = 0,        // World normal (encoded)         → R10G10B10A2_UNORM
  kMaterial = 1,      // Metallic, specular, roughness  → R8G8B8A8_UNORM
  kBaseColor = 2,     // Base color, AO                 → R8G8B8A8_SRGB
  kCustomData = 3,    // Custom data / shading model    → R8G8B8A8_UNORM
  kShadowFactors = 4, // Shadow factors (reserved)
  kWorldTangent = 5,  // World tangent (reserved)

  kCount = 6,
  kActiveCount = 4,  // Phase-1: A-D only
};
```

**File:** `SceneRenderer/SceneTextures.h`

### 3.3 SceneTextures

Concrete product family. Owns GPU texture resources.

```cpp
class SceneTextures {
public:
  OXGN_VRTX_API explicit SceneTextures(
    graphics::IGraphics& gfx,
    const SceneTexturesConfig& config);

  OXGN_VRTX_API ~SceneTextures();

  // Non-copyable, non-movable (owns GPU resources)
  SceneTextures(const SceneTextures&) = delete;
  auto operator=(const SceneTextures&) -> SceneTextures& = delete;
  SceneTextures(SceneTextures&&) = delete;
  auto operator=(SceneTextures&&) -> SceneTextures& = delete;

  // --- Core products (always valid after construction) ---

  [[nodiscard]] OXGN_VRTX_API auto GetSceneColor() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneDepth() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetPartialDepth() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetStencil() const
    -> graphics::Texture&;

  // --- GBuffer products (valid after RebuildWithGBuffers) ---

  [[nodiscard]] OXGN_VRTX_API auto GetGBuffer(GBufferIndex index) const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferCount() const
    -> std::uint32_t;

  // --- Optional products (null when not enabled) ---

  [[nodiscard]] OXGN_VRTX_API auto GetVelocity() const
    -> graphics::Texture*;
  [[nodiscard]] OXGN_VRTX_API auto GetCustomDepth() const
    -> graphics::Texture*;
  [[nodiscard]] OXGN_VRTX_API auto GetCustomStencil() const
    -> graphics::Texture*;

  // --- Lifecycle ---

  OXGN_VRTX_API void Resize(glm::uvec2 new_extent);
  OXGN_VRTX_API void RebuildWithGBuffers();

  // --- Query ---

  [[nodiscard]] OXGN_VRTX_API auto GetExtent() const -> glm::uvec2;
  [[nodiscard]] OXGN_VRTX_API auto GetConfig() const
    -> const SceneTexturesConfig&;

private:
  graphics::IGraphics& gfx_;
  SceneTexturesConfig config_;

  // Core products — allocated at construction
  std::unique_ptr<graphics::Texture> scene_color_;     // R16G16B16A16_FLOAT
  std::unique_ptr<graphics::Texture> scene_depth_;     // D32_FLOAT_S8X24_UINT (depth + scene stencil family)
  std::unique_ptr<graphics::Texture> partial_depth_;   // R32_FLOAT

  // GBuffer products — allocated at construction, valid after rebuild
  std::array<std::unique_ptr<graphics::Texture>,
    static_cast<size_t>(GBufferIndex::kCount)> gbuffers_;

  // Optional products — null when not enabled
  std::unique_ptr<graphics::Texture> velocity_;        // R16G16_FLOAT
  std::unique_ptr<graphics::Texture> custom_depth_;    // D32_FLOAT_S8X24_UINT when enabled (custom depth + custom stencil family)
};
```

**File:** `SceneRenderer/SceneTextures.h` + `SceneRenderer/SceneTextures.cpp`

Getter semantics:

- `GetStencil()` exposes the scene stencil family through the scene
  depth/stencil resource owned by `SceneTextures`
- `GetCustomStencil()` exposes the custom stencil family only when the optional
  custom depth/stencil path is enabled

### 3.4 SceneTextureSetupMode

Tracks which products are set up and bindable at a given runtime point.

```cpp
class SceneTextureSetupMode {
public:
  enum class Flag : std::uint32_t {
    kNone            = 0,
    kSceneDepth      = 1 << 0,
    kPartialDepth    = 1 << 1,
    kSceneVelocity   = 1 << 2,   // partial or complete
    kGBuffers        = 1 << 3,   // A-D valid for reads
    kSceneColor      = 1 << 4,   // written by base pass
    kStencil         = 1 << 5,
    kCustomDepth     = 1 << 6,
  };

  void Set(Flag flag);
  void Clear(Flag flag);
  void Reset();  // back to kNone at frame start

  [[nodiscard]] auto IsSet(Flag flag) const -> bool;
  [[nodiscard]] auto GetFlags() const -> std::uint32_t;

private:
  std::uint32_t flags_{0};
};
```

**Ownership:** `SceneRenderer` owns the single instance and updates it at
stage boundaries. Passes consume the mode; they do not update it.

**Setup milestones (ARCHITECTURE.md §7.3.3):**

| After Stage | Flags Set |
| ----------- | --------- |
| 3 (depth prepass) | `kSceneDepth`, `kPartialDepth`, optionally `kSceneVelocity` (partial) |
| 9 (base pass) | `kGBuffers`, `kSceneColor` added |
| 10 (rebuild) | GBuffer-valid state for deferred consumers |
| 22 (post-process) | All production products valid |
| 23 (cleanup) | Extraction set queued |

**File:** `SceneRenderer/SceneTextures.h` (same header, separate class)

### 3.5 SceneTextureBindings

Bindless routing metadata, derived from SceneTextures + setup mode. This is
NOT a UBO or parameter block — it is metadata that tells shaders how to reach
scene products through bindless handles.

```cpp
struct SceneTextureBindings {
  // Bindless SRV indices for scene products
  std::uint32_t scene_color_srv{kInvalidIndex};
  std::uint32_t scene_depth_srv{kInvalidIndex};
  std::uint32_t partial_depth_srv{kInvalidIndex};
  std::uint32_t velocity_srv{kInvalidIndex};
  std::uint32_t stencil_srv{kInvalidIndex};
  std::uint32_t custom_depth_srv{kInvalidIndex};
  std::uint32_t custom_stencil_srv{kInvalidIndex};

  // GBuffer SRV indices
  std::array<std::uint32_t,
    static_cast<size_t>(GBufferIndex::kActiveCount)> gbuffer_srvs{};

  // UAV indices for write access (stage-specific)
  std::uint32_t scene_color_uav{kInvalidIndex};

  // Validity tracking (mirrors setup mode for shader safety)
  std::uint32_t valid_flags{0};

  static constexpr std::uint32_t kInvalidIndex =
    std::numeric_limits<std::uint32_t>::max();
};
```

**Ownership:** `SceneRenderer` owns the current `SceneTextureBindings`
instance as the canonical routing metadata for the current frame/view setup
state.

**Generation:** `SceneRenderer` regenerates `SceneTextureBindings` whenever
`SceneTextureSetupMode` changes. It uses the graphics layer's descriptor
allocation to create SRV views for set-up textures and writes
`kInvalidIndex` for products not yet set up. `stencil_srv` routes the scene
stencil family from the scene depth/stencil resource. `custom_stencil_srv`
routes the custom stencil family only when the optional custom depth/stencil
path is enabled.

**Publication:** `SceneTextureBindings` is published into `ViewFrameBindings`
through Renderer Core publication helpers so that passes can access scene
products through the standard per-view binding stack. Publication is an
explicit renderer-side step; passes never synthesize or own shared
scene-texture routing metadata.

**File:** `SceneRenderer/SceneTextures.h`

### 3.6 SceneTextureExtracts

Handoff artifacts produced during post-render cleanup.

```cpp
struct SceneTextureExtractRef {
  graphics::Texture* texture{nullptr};  // extracted/handoff artifact
  bool valid{false};
};

struct SceneTextureExtracts {
  // Resolved outputs for external consumers
  SceneTextureExtractRef resolved_scene_color;
  SceneTextureExtractRef resolved_scene_depth;

  // History textures for next-frame reuse
  SceneTextureExtractRef prev_scene_depth;
  SceneTextureExtractRef prev_velocity;
};
```

**Ownership:** `SceneRenderer` produces this during `PostRenderCleanup`
(stage 23). Renderer Core helper surfaces may consume extracted artifacts for
composition handoff or history management. The extract refs are non-owning
descriptors for explicit handoff artifacts; they are not permission to treat
live `SceneTextures` attachments as extracted outputs.

**File:** `SceneRenderer/SceneTextures.h`

## 4. Data Flow and Dependencies

### 4.1 Product Lifecycle Within a Frame

```text
Frame Start
  └─ SceneTextures exist (allocated at construction or after resize)
  └─ SceneTextureSetupMode::Reset() → kNone
  └─ SceneTextureBindings invalidated

Stage 3 (Depth Prepass)
  └─ Writes: SceneDepth, PartialDepth, partial SceneVelocity
  └─ SetupMode += kSceneDepth | kPartialDepth | kSceneVelocity (partial)
  └─ Bindings regenerated with depth SRVs

Stage 9 (Base Pass)
  └─ Writes: GBufferNormal/Material/BaseColor/CustomData (MRT), SceneColor (emissive), velocity completion
  └─ SetupMode += kGBuffers | kSceneColor

Stage 10 (Rebuild)
  └─ SceneTextures::RebuildWithGBuffers() — state transition
  └─ Bindings regenerated with full GBuffer SRVs
  └─ GBuffers now readable by downstream stages

Stage 12 (Deferred Lighting)
  └─ Reads: GBufferNormal/Material/BaseColor/CustomData, SceneDepth, shadow data, IBL
  └─ Writes: SceneColor (accumulated lighting)

Stage 22 (Post-Process)
  └─ Reads: SceneColor, SceneDepth, Velocity

Stage 23 (Cleanup)
  └─ SceneTextureExtracts produced
  └─ History textures handed off for next frame
```

### 4.2 Dependency Direction

| Component | Depends On | Depended On By |
| --------- | ---------- | -------------- |
| SceneTextures | Graphics layer (IGraphics, Texture) | SceneRenderer, all stage modules, all services |
| SceneTextureSetupMode | None | SceneTextureBindings generation, stage/service consumers |
| SceneTextureBindings | SceneTextures + SetupMode + Graphics (descriptor alloc) | Renderer Core publication helpers → RenderContext/ViewFrameBindings → passes |
| SceneTextureExtracts | SceneTextures | Renderer Core handoff surfaces |

## 5. Resource Management

### 5.1 GPU Resources

| Product | Format | Size | Lifecycle |
| ------- | ------ | ---- | --------- |
| SceneColor | `R16G16B16A16_FLOAT` | `extent.x × extent.y` | Persistent, resized on viewport change |
| SceneDepth | `D32_FLOAT_S8X24_UINT` | `extent.x × extent.y` | Persistent; carries scene depth + scene stencil family |
| PartialDepth | `R32_FLOAT` | `extent.x × extent.y` | Persistent |
| Stencil | Scene/custom stencil family | `extent.x × extent.y` | Routed from the stencil aspect of `SceneDepth`, and from `CustomDepth` when the optional custom path is enabled |
| GBufferNormal | `R10G10B10A2_UNORM` | `extent.x × extent.y` | Persistent |
| GBufferMaterial | `R8G8B8A8_UNORM` | `extent.x × extent.y` | Persistent |
| GBufferBaseColor | `R8G8B8A8_SRGB` | `extent.x × extent.y` | Persistent |
| GBufferCustomData | `R8G8B8A8_UNORM` | `extent.x × extent.y` | Persistent |
| Velocity | `R16G16_FLOAT` | `extent.x × extent.y` | Persistent (if enabled) |
| CustomDepth | `D32_FLOAT_S8X24_UINT` | `extent.x × extent.y` | Persistent (if enabled); carries custom depth + optional custom stencil family |

### 5.2 Allocation Strategy

All textures are allocated at construction time based on
`SceneTexturesConfig`. `GBufferShadowFactors` / `GBufferWorldTangent` remain
reserved in the array but are not
allocated until `gbuffer_count > 4`.

### 5.3 Resize Behavior

`Resize(new_extent)` destroys and recreates all textures at the new
dimensions. This is a full reallocation, not a view re-creation. After resize,
`SceneTextureSetupMode` resets to `kNone`.

**When called:** At frame start if viewport changed. The SceneRenderer checks
viewport dimensions against current extent and calls `Resize` if they differ.

### 5.4 RebuildWithGBuffers

`RebuildWithGBuffers()` is NOT a reallocation. It is a state transition that:

1. Validates GBuffer textures exist and have been written
2. Creates or updates SRV descriptor views for GBuffer read access
3. Leaves ownership of `SceneTextureSetupMode` to `SceneRenderer`, which marks
   GBuffers as consumable and republishes shared routing metadata after the
   rebuild boundary

This is called by `SceneRenderer` at stage 10 after the base pass completes.

## 6. Shader Contracts

SceneTextures is consumed by shaders through `SceneTextureBindings`. The
shader-side contract is defined in the shader-contracts LLD
([shader-contracts.md](shader-contracts.md)). The CPU-side binding generation
is defined in §3.5 above.

Key shader-facing files:

- `Shaders/Vortex/Contracts/SceneTextures.hlsli` — accessor functions
- `Shaders/Vortex/Contracts/SceneTextureBindings.hlsli` — bindless index
  declarations

The shader-side binding contract must expose the scene/custom stencil family in
a way that matches the CPU-side routing metadata. Phase 2 may keep the custom
stencil route inactive when `enable_custom_depth == false`, but it must not
design the stencil family as scene-depth-only.

## 7. Stage Integration

SceneTextures is owned by `SceneRenderer` and passed by reference to every
stage module and subsystem service that needs it:

```cpp
// Stage module dispatch signature
void XxxModule::Execute(RenderContext& ctx, SceneTextures& scene_textures);

// Subsystem service domain methods
void LightingService::RenderDeferredLighting(
  RenderContext& ctx, const SceneTextures& scene_textures);
```

**Null-safe behavior:** SceneTextures is never null — it exists from
SceneRenderer construction. Optional products (velocity, custom depth) may
be null if not enabled; accessors return `nullptr` for disabled optional
products such as `CustomDepth` / `CustomStencil`.

## 8. Testability Approach

### 8.1 Unit Tests

1. **Construction:** Create `SceneTextures` with various configs. Verify all
   expected textures are allocated and unexpected ones are null.
2. **Resize:** Resize and verify extent changes, textures recreated.
3. **SetupMode:** Set and query flags. Verify milestone transitions.
4. **Stencil/custom-depth contract:** Verify the scene depth resource exposes
   the scene stencil family, and the optional custom depth path exposes custom
   depth plus custom stencil routing when enabled.
5. **RebuildWithGBuffers:** Call after setup, verify GBuffer SRVs are valid and
   binding regeneration is required before downstream use.
6. **Config validation:** Invalid configs (zero extent) produce errors.

### 8.2 Integration Tests

1. **Frame lifecycle:** SceneTextures survives a full frame cycle with
   setup mode progression.
2. **Bindings generation:** After each setup milestone, verify bindings
   have valid SRV indices for set-up products and `kInvalidIndex` for others.
3. **Extraction semantics:** Verify `SceneTextureExtracts` describes explicit
   handoff artifacts rather than aliasing live in-frame attachments.

### 8.3 RenderDoc Validation

At frame 10 baseline:

- Verify SceneColor, SceneDepth, and GBuffer textures appear in the resource
  list with expected formats and dimensions.
- Verify the first active subset is present and queryable: `SceneColor`,
  `SceneDepth`, `PartialDepth`, `GBufferNormal`/`Material`/`BaseColor`/`CustomData`, `Stencil`, `Velocity`, and
  `CustomDepth`.
- Verify stencil-family routing exists through the scene depth/stencil resource
  and, when enabled, through the custom depth/stencil resource.

## 9. Open Questions

None. The Phase 2 contract is fully specified after clarifying:

- the first active subset includes explicit `Stencil` and `CustomDepth`
  coverage
- the stencil family spans scene and optional custom stencil routing
- `SceneRenderer` owns both setup state and shared bindless routing metadata
- extraction describes explicit handoff artifacts rather than live scene
  attachments
