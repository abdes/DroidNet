# Screen HZB Low-Level Design

**Phase:** 5C — Occlusion / HZB
**Deliverable:** D.16 supplement
**Status:** `ready`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

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

Both pyramids are persistent per-view products and support previous-frame
handoff through the module's double-buffered history.

### 1.3 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 3 (DepthPrepass) | Current scene depth product is established |
| Predecessor | Stage 4 (reserved — GeometryVirtualization) | |
| **This** | **Stage 5 — Occlusion / HZB** | Current-frame HZB pyramids produced |
| Successor | Later stages | May consume the published HZB products if requested |

### 1.4 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — runtime stage table
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [occlusion.md](occlusion.md) — Stage 5 umbrella scope
- [hzb-parity-remediation.md](hzb-parity-remediation.md) — UE5.7 parity closure record

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

| Constant | Value | Meaning |
| -------- | ----- | ------- |
| `kScreenHzbFrameBindingsFlagAvailable` | `1 << 0` | HZB was built this frame |
| `kScreenHzbFrameBindingsFlagFurthestValid` | `1 << 1` | `furthest_srv` is valid |
| `kScreenHzbFrameBindingsFlagClosestValid` | `1 << 2` | `closest_srv` is valid |

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

For each requested mip level, `VortexScreenHzbBuildCS` is dispatched once with
an `8 x 8` thread-group grid covering the output mip. Per-mip constants are
uploaded through a constant-buffer-aligned slot buffer.

```text
Execute()
  ├─ determine active source view rect
  ├─ compute HZB root extent + mip count
  ├─ ensure per-view history + scratch resources
  ├─ ensure pass-constants buffer
  ├─ select write history slot
  ├─ for each mip:
  │    ├─ write per-mip constants
  │    ├─ dispatch compute reduction
  │    └─ copy single-mip scratch output into history texture mip
  ├─ transition written history textures to ShaderResource
  ├─ swap history slot
  └─ build current/previous Output values
```

### 4.2 Source Sampling Strategy

- **Mip 0** samples `SceneDepth` over the active source view rect.
- **Mip N > 0** samples the previous scratch result for that pyramid.

Sub-viewport source origins are passed through constants so the build never
bleeds into adjacent regions of the scene texture.

### 4.3 Conservative Reduction

Each output texel reduces a clamped `2 x 2` source neighbourhood:

```text
closest_depth = max(source samples)
furthest_depth = min(source samples)
```

Under reversed-Z:

- `max` yields the closest surface
- `min` yields the furthest surface

### 4.4 Scratch Ping-Pong

Each pyramid uses two single-mip scratch textures:

- write scratch slot = `mip_level & 1`
- read scratch slot = `(mip_level & 1) ^ 1`

After each dispatch, the written scratch mip is copied into the corresponding
mip slice of the persistent history texture.

## 5. Resource Management

### 5.1 Per-View State

Each active `ViewId` owns a persistent `ViewState` in the module `Impl`.
Resources are recreated when root extent, mip count, or requested pyramid set
changes.

### 5.2 Texture Layout

| Resource | Slots | Format | Mips | Usage |
| -------- | :---: | ------ | :--: | ----- |
| `closest.history_textures[0/1]` | 2 | `R32_FLOAT` | full chain | persistent SRV |
| `furthest.history_textures[0/1]` | 2 | `R32_FLOAT` | full chain | persistent SRV |
| `closest.scratch_textures[0/1]` | 2 | `R32_FLOAT` | 1 | UAV + copy source |
| `furthest.scratch_textures[0/1]` | 2 | `R32_FLOAT` | 1 | UAV + copy source |

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

| Function | Purpose |
| -------- | ------- |
| `GetHzbSize()` | root mip extent |
| `GetHzbViewSize()` | active view size |
| `GetHzbViewRect()` | UE5-style view-local rect |
| `GetViewportUvToHzbBufferUv()` | viewport UV → HZB UV scale |
| `GetHzbUvFactorAndInvFactor()` | scale + inverse scale |
| `GetHzbUvToScreenUvScaleBias()` | HZB UV → screen UV affine |
| `GetHzbBaseTexelSize()` | mip-0 texel size |
| `GetSamplePixelToHzbUv()` | pixel-centre → HZB UV |
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

### 9.2 Visual Validation

Use `scene-depth-linear` only to validate the source depth product before HZB
build. Use GPU markers such as `Vortex.Stage5.ScreenHzbBuild` to verify HZB
dispatch count and placement. This document does not define any dedicated HZB
consumer visualization.

## 10. Invariants

1. `ScreenHzbModule::Execute()` is called only when current HZB is requested
   and a valid current depth product exists.
2. The module does not write back into `SceneTextures`.
3. Scratch textures are single-mip; history textures carry the full chain.
4. `GetCurrentOutput()` and `GetPreviousOutput()` are stable after `Execute()`
   returns and are reset at the start of the next `Execute()`.
5. Consumers must treat `ScreenHzbFrameBindings` as frame-local published data,
   not as cross-frame cached state.

## 11. Open Questions

1. Future GPU-driven occlusion/query systems may require additional GPU-side
   publication surfaces layered on top of the generic HZB products defined
   here.
