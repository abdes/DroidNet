# Screen HZB Low-Level Design

**Phase:** 5C — Occlusion / HZB
**Deliverable:** D.16 supplement
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`ScreenHzbModule` is the Stage 5 owner responsible for building the generic
screen-space **Hierarchical Z-Buffer (HZB)** product from the current frame's
`SceneDepth`.

This document covers only:

- HZB production
- HZB resource ownership
- HZB publication
- the generic consumption model of HZB products

This document does **not** specify:

- any specific consumer stage's request policy
- any specific consumer stage's sampling algorithm
- occlusion-query batching or GPU-driven culling policy

Those belong in the owning consumer or higher-level Stage 5 design docs.

### 1.2 Product Shape

The module can produce up to two independent mip-chain pyramids per view:

- **Closest** — conservative maximum depth per texel neighbourhood under
  reversed-Z semantics
- **Furthest** — conservative minimum depth per texel neighbourhood under
  reversed-Z semantics

Persistent views retain both pyramids and support previous-frame handoff through
double-buffered history. Actual view retirement erases the HZB cache entry and
fence-retires all history/scratch textures with their descriptor registrations.
Inactive persistent views retain history. Stateless invocations retain only
their current outputs through consumers and GPU retirement; they do not leave
ViewId-owned history entries. Retired/current output references are cleared at
their publication boundary so later views cannot observe stale indices.

Each dispatch uses an immutable 48-byte structured constants record from the
existing frame-retained publisher, with matching C++/HLSL layout. Repeated
same-frame resets do not rewind its allocation cursor. Slot reuse occurs only
through the existing frame-retirement contract; no GPU wait is introduced.

The module also builds transient, unpublished occlusion pyramids for culling
views (§4.5).

### 1.3 Stage Position

| Position    | Stage                                       | Notes                                               |
| ----------- | ------------------------------------------- | --------------------------------------------------- |
| Predecessor | Stage 3 (DepthPrepass)                      | Current scene depth product is established          |
| Predecessor | Stage 4 (reserved — GeometryVirtualization) |                                                     |
| **This**    | **Stage 5 — Occlusion / HZB**               | Current-frame HZB pyramids produced                 |
| Successor   | Later stages                                | May consume the published HZB products if requested |

### 1.4 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — runtime stage table
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [occlusion.md](occlusion.md) — Stage 5 umbrella scope
- [HZB consumer qualification](../milestones/VTX-M05B/README.md) — UE5.7 parity closure record

### 1.5 Classification

`ScreenHzbModule` is a stage module with per-view persistent history state.
HZB textures are stored on per-view `ViewState` inside the module `Impl`,
not in `SceneTextures`.

### 1.6 Preconditions

Stage 5 HZB execution is gated by:

```text
ctx.current_view.CanBuildScreenHzb()
  == ctx.current_view.screen_hzb_request.WantsCurrentHzb()
  && ctx.current_view.scene_depth_product_valid
```

This document intentionally does not define how specific pipelines decide
which pyramids they request. It defines only the generic requirement:

- a valid current depth product must exist
- at least one HZB pyramid must be requested

If neither pyramid is requested, `ScreenHzbModule::Execute()` returns with no
work and no output.

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── Hzb/
            ├── ScreenHzbModule.h
            └── ScreenHzbModule.cpp

src/Oxygen/Vortex/
└── Types/
    └── ScreenHzbFrameBindings.h

src/Oxygen/Graphics/Direct3D12/
└── Shaders/
    └── Vortex/
        ├── Stages/
        │   └── Occlusion/
        │       └── ScreenHzbBuild.hlsl
        └── Contracts/
            └── Scene/
                └── ScreenHzbBindings.hlsli
```

### 2.2 C++ Module API

```cpp
namespace oxygen::vortex {

class ScreenHzbModule {
public:
  struct Output {
    std::shared_ptr<const graphics::Texture> closest_texture {};
    std::shared_ptr<const graphics::Texture> furthest_texture {};
    ScreenHzbFrameBindings bindings {};
    bool available { false };
  };

  explicit ScreenHzbModule(
    Renderer& renderer, const SceneTexturesConfig& scene_textures_config);
  ~ScreenHzbModule();

  ScreenHzbModule(const ScreenHzbModule&) = delete;
  auto operator=(const ScreenHzbModule&) -> ScreenHzbModule& = delete;
  ScreenHzbModule(ScreenHzbModule&&) = delete;
  auto operator=(ScreenHzbModule&&) -> ScreenHzbModule& = delete;

  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  [[nodiscard]] auto GetCurrentOutput() const -> const Output&;
  [[nodiscard]] auto GetPreviousOutput() const -> const Output&;
};

} // namespace oxygen::vortex
```

`ScreenHzbModule` is owned by `SceneRenderer` as `screen_hzb_` for the
renderer lifetime.

### 2.3 Published HZB Contract

`ScreenHzbFrameBindings` is a 128-byte, 16-byte-aligned, standard-layout value
published to shaders. It mirrors the UE5.7-computable HZB common parameter
surface plus bindless texture handles and mip count.

```cpp
struct alignas(16) ScreenHzbFrameBindings {
  ShaderVisibleIndex closest_srv;
  ShaderVisibleIndex furthest_srv;
  uint32_t width;
  uint32_t height;
  uint32_t mip_count;
  uint32_t flags;

  float hzb_size_x;
  float hzb_size_y;
  float hzb_view_size_x;
  float hzb_view_size_y;

  int32_t hzb_view_rect_min_x;
  int32_t hzb_view_rect_min_y;
  int32_t hzb_view_rect_width;
  int32_t hzb_view_rect_height;

  float viewport_uv_to_hzb_buffer_uv_x;
  float viewport_uv_to_hzb_buffer_uv_y;
  float hzb_uv_factor_x;
  float hzb_uv_factor_y;
  float hzb_uv_inv_factor_x;
  float hzb_uv_inv_factor_y;

  float hzb_uv_to_screen_uv_scale_x;
  float hzb_uv_to_screen_uv_scale_y;
  float hzb_uv_to_screen_uv_bias_x;
  float hzb_uv_to_screen_uv_bias_y;

  float hzb_base_texel_size_x;
  float hzb_base_texel_size_y;
  float sample_pixel_to_hzb_uv_x;
  float sample_pixel_to_hzb_uv_y;
  float screen_pos_to_hzb_uv_scale_x;
  float screen_pos_to_hzb_uv_scale_y;
  float screen_pos_to_hzb_uv_bias_x;
  float screen_pos_to_hzb_uv_bias_y;
};
```

#### Flags

| Constant                                   | Value    | Meaning                  |
| ------------------------------------------ | -------- | ------------------------ |
| `kScreenHzbFrameBindingsFlagAvailable`     | `1 << 0` | HZB was built this frame |
| `kScreenHzbFrameBindingsFlagFurthestValid` | `1 << 1` | `furthest_srv` is valid  |
| `kScreenHzbFrameBindingsFlagClosestValid`  | `1 << 2` | `closest_srv` is valid   |

### 2.4 `HZBViewRect` Semantics

The current-frame `HZBViewRect` fields in `ScreenHzbFrameBindings` follow the
UE5.7 common-parameter contract:

```text
HZBViewRect = int4(0, 0, ViewRect.Width(), ViewRect.Height())
```

That means:

- `hzb_view_rect_min_x == 0`
- `hzb_view_rect_min_y == 0`
- `hzb_view_rect_width == active view width`
- `hzb_view_rect_height == active view height`

They are **not** the original scene-texture viewport origin. The original
view-rect origin is used internally when building the HZB and when deriving
screen-position mappings, but it is not published here as `HZBViewRect`.

Previous-view rect data is a separate history concern and is not part of the
current-frame HZB common parameter itself.

## 3. Pyramid Geometry

### 3.1 Extent Derivation

The HZB root mip is half the active source view extent, rounded up to the next
power of two per axis:

```text
hzb_root_extent(e) = max(bit_ceil(e) >> 1, 1)
```

Examples:

- viewport `1920 x 1080` → root `1024 x 1024`
- viewport `512 x 512` → root `256 x 256`
- viewport `128 x 72` → root `64 x 64`

### 3.2 Mip Count

```text
mip_count = max(bit_width(max(width, height)) - 1, 1)
```

For a `1024 x 1024` root:

- `bit_width(1024) = 11`
- `mip_count = 11 - 1 = 10`

So the chain is:

- mip 0 = `1024 x 1024`
- ...
- mip 9 = `2 x 2`

The minimum mip count is 1.

### 3.3 Per-Mip Extent

```text
mip_extent(base, level) = max(1, base >> level)
```

## 4. Build Algorithm

### 4.1 Overview

Each pyramid is built by one dispatch of a single-pass downsampler (reference:
AMD FidelityFX SPD):

1. Each 256-thread workgroup reduces a 64 x 64 tile of mip 0 down to mip 5,
   in group-shared memory.
2. The workgroup writes every mip it produces directly to the pyramid
   texture's per-mip UAVs.
3. The last workgroup to finish, detected by a global atomic counter, reduces
   mips 6-11 from mip 5.

The last-workgroup pattern needs no forward progress between workgroups. Mips
above 11 (roots over 4096) use one further dispatch of the same shader.

```text
Execute()
  ├─ determine active source view rect
  ├─ compute HZB root extent + mip count
  ├─ ensure per-view history resources and the atomic counter
  ├─ select write history slot
  ├─ dispatch the single-pass build for each requested pyramid
  ├─ transition written history textures to ShaderResource
  ├─ swap history slot
  └─ build current/previous Output values
```

### 4.2 Source Sampling Strategy

- **Mip 0** reduces `SceneDepth` over the active source view rect.
- **Mip N > 0** reduces mip N - 1 of the same pyramid.

Sub-viewport source origins are passed through constants so the build never
bleeds into adjacent regions of the scene texture.

### 4.3 Conservative Reduction

The root extent is a power of two at most the source extent (§3.1), so the
source-to-mip-0 ratio `r` per axis lies in `(1, 2]` and is generally
fractional. Mip-0 texel `x` covers source pixels `floor(x * r)` through
`ceil((x + 1) * r) - 1`: up to three per axis.

- Mip 0 reduces every source pixel of that footprint, up to 3 x 3, clamped to
  the view rect. A fixed 2 x 2 neighbourhood misses pixels when `r` is
  fractional, and occluder depth would then be lost.
- Mips N > 0 reduce exact 2 x 2 blocks of mip N - 1, clamped at odd edges.

```text
closest_depth = max(source samples)
furthest_depth = min(source samples)
```

Under reversed-Z:

- `max` yields the closest surface
- `min` yields the furthest surface

### 4.4 Removed Scratch Ping-Pong

The previous build dispatched once per mip into single-mip scratch textures
and copied each into the history texture: two GPU operations per mip, about
20 per pyramid. The single-pass build writes the pyramid directly, so the
scratch textures and copies are removed.

### 4.5 Occlusion Pyramid

`BuildOcclusionPyramid(ctx, depth_source, culling_view)` builds a
furthest-only pyramid for [occlusion culling](occlusion.md#43-occlusion-pyramid):

- **Source.** A depth texture and rect: phase 1 `SceneDepth` for a camera
  view, or phase 1 shadow depth for a shadow view.
- **Shape.** Same extent rules (§3) and build (§4.1-§4.3) as the Screen HZB.
- **Storage.** Writes one transient texture owned per culling view, recreated
  only when its extent changes.
- **Publication.** It is never published through `ScreenHzbFrameBindings` and
  never enters HZB history.
- **Bindings.** It returns its SRV, extent and mip count for the occlusion
  kernels.

## 5. Resource Management

### 5.1 Per-View State

Each active `ViewId` owns a persistent `ViewState` in the module `Impl`.
Resources are recreated when root extent, mip count, or requested pyramid set
changes.

### 5.2 Texture Layout

| Resource                             | Slots | Format      |    Mips    | Usage               |
| ------------------------------------ | :---: | ----------- | :--------: | ------------------- |
| `closest.history_textures[0/1]`      |   2   | `R32_FLOAT` | full chain | per-mip UAV + SRV   |
| `furthest.history_textures[0/1]`     |   2   | `R32_FLOAT` | full chain | per-mip UAV + SRV   |
| occlusion pyramid (per culling view) |   1   | `R32_FLOAT` | full chain | per-mip UAV + SRV   |
| single-pass atomic counter           |   1   | `R32_UINT`  |     -      | UAV, self-resetting |

### 5.3 Previous-Frame Handoff

The module keeps two history slots per pyramid and alternates between them:

```text
write_slot = current_history_slot ^ 1
```

After a successful build:

- `GetCurrentOutput()` exposes the just-written slot
- `GetPreviousOutput()` exposes the previously current slot when available

## 6. HZB Publication Contract

This section defines only the generic HZB publication surface.

It does not define any consumer-specific behavior beyond the generic
requirement that consumers read HZB through published per-view products.

### 6.1 Published Products

The generic HZB publication surface consists of:

1. current-frame `ScreenHzbFrameBindings`
2. current-frame closest/furthest SRV handles and validity flags
3. previous-frame furthest-HZB availability through the module output path
4. current-frame bindless routing through `ViewFrameBindings::screen_hzb_frame_slot`

### 6.2 Publication Sequence

```text
Stage 5:
  1. ScreenHzbModule::Execute(ctx, scene_textures)
  2. GetCurrentOutput() populates current-frame HZB publication
  3. PublishScreenHzbProducts(ctx) publishes the frame-slot routing
  4. GetPreviousOutput() exposes previous-frame furthest HZB when available
```

If current-frame HZB is not available, the current view's HZB frame slot stays
invalid for that frame.

## 7. Generic Consumption Model

### 7.1 Access Pattern

Consumers access HZB through `ScreenHzbBindings.hlsli`:

```hlsl
ScreenHzbFrameBindingsData hzb
    = LoadScreenHzbBindings(view_frame_bindings.screen_hzb_frame_slot);

if (!IsScreenHzbAvailable(hzb)) { /* no HZB this frame */ }

float2 hzb_uv = viewport_uv * GetViewportUvToHzbBufferUv(hzb);
Texture2D<float> pyramid = ResourceDescriptorHeap[hzb.furthest_srv];
float depth = pyramid.SampleLevel(point_sampler, hzb_uv, desired_mip);
```

### 7.2 Generic Rules

- Consumers must gate reads on `IsScreenHzbAvailable()`.
- Consumers must gate pyramid-specific reads on the corresponding validity bit.
- Consumers must use the published coordinate transforms instead of
  reconstructing mappings from raw dimensions.
- Consumers must apply reversed-Z depth semantics consistently.

### 7.3 Provided Mapping Helpers

| Function                         | Purpose                         |
| -------------------------------- | ------------------------------- |
| `GetHzbSize()`                   | root mip extent                 |
| `GetHzbViewSize()`               | active view size                |
| `GetHzbViewRect()`               | UE5-style view-local rect       |
| `GetViewportUvToHzbBufferUv()`   | viewport UV → HZB UV scale      |
| `GetHzbUvFactorAndInvFactor()`   | scale + inverse scale           |
| `GetHzbUvToScreenUvScaleBias()`  | HZB UV → screen UV affine       |
| `GetHzbBaseTexelSize()`          | mip-0 texel size                |
| `GetSamplePixelToHzbUv()`        | pixel-centre → HZB UV           |
| `GetScreenPosToHzbUvScaleBias()` | screen-position → HZB UV affine |

## 8. Coordinate Space Conventions

### 8.1 Reversed-Z

The engine uses reversed-Z:

- depth near `1.0` = near plane
- depth near `0.0` = far plane

Therefore:

- closest pyramid uses `max`
- furthest pyramid uses `min`

### 8.2 Viewport UV → HZB UV

The current-frame mapping is:

```text
hzb_uv = viewport_uv * ViewportUVToHZBBufferUV
```

This remains correct for sub-viewports and power-of-two-padded HZB roots.

### 8.3 Mip Selection

Mip 0 is the finest HZB level. Higher mips cover larger screen areas.
Consumers typically choose a mip proportional to the projected screen-space
extent they are testing.

## 9. Testability and Validation

### 9.1 Unit / Integration Proof

1. `ComputeHzbRootExtent` and `ComputeMipCount` handle edge cases correctly.
2. `ScreenHzbFrameBindings` layout/alignment stays stable.
3. current-frame publication proves root extent, mip count, and validity flags.
4. sub-viewport publication proves the mapping terms are derived from the
   active view rect.
5. previous-frame publication proves prior furthest-HZB availability after at
   least two consecutive frames.
6. coverage: for non-power-of-two sources (for example 1920 x 1080 and
   1366 x 768), a single foreground pixel at every source position reaches
   every mip of both pyramids.
7. the occlusion pyramid matches a CPU reference reduction of the same source
   and is absent from `ScreenHzbFrameBindings` and HZB history.

### 9.2 Visual Validation

Use `scene-depth-linear` only to validate the source depth product before HZB
build. Use GPU markers such as `Vortex.Stage5.ScreenHzbBuild` to verify HZB
dispatch count and placement. This document does not define any dedicated HZB
consumer visualization.

## 10. Invariants

1. `ScreenHzbModule::Execute()` is called only when current HZB is requested
   and a valid current depth product exists.
2. The module does not write back into `SceneTextures`.
3. Each pyramid is built by one single-pass dispatch (two above 4096 roots);
   history and occlusion textures carry the full chain.
4. `GetCurrentOutput()` and `GetPreviousOutput()` are stable after `Execute()`
   returns and are reset at the start of the next `Execute()`.
5. Consumers must treat `ScreenHzbFrameBindings` as frame-local published data,
   not as cross-frame cached state.
