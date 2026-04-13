# Offscreen Rendering LLD

**Phase:** 5E — Remaining Services
**Deliverable:** D.18
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

Validation and adaptation of the three canonical non-runtime facades for
the Vortex substrate:

- `ForSinglePassHarness()` — single pass, validated context
- `ForRenderGraphHarness()` — render graph, validated context
- `ForOffscreenScene()` — scene-derived offscreen rendering

No new facades are introduced. Existing Oxygen facades are adapted to
work with the Vortex substrate.

### 1.2 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) — non-runtime facade contracts
- PRD §6 — offscreen rendering requirements
- Existing facade implementations in the engine

## 2. Facade Contracts

### 2.1 ForSinglePassHarness

**Purpose:** Execute a single validated pass for testing or tooling.

```cpp
auto ForSinglePassHarness(
    graphics::IGraphics& gfx,
    const HarnessConfig& config)
    -> std::unique_ptr<RenderHarness>;
```

**Vortex adaptation:**

- Creates a minimal Vortex `Renderer` with only the required capabilities
- Executes one validated pass against the Vortex substrate without requiring
  the full runtime scene path
- Validates that the pass produces correct output to the provided
  render target

**Validation tasks (Phase 5E):**

1. Create harness with Vortex backend
2. Execute a depth-only pass → verify depth output
3. Execute a base-pass → verify GBuffer output
4. Execute a tonemap pass → verify LDR output

### 2.2 ForRenderGraphHarness

**Purpose:** Execute a caller-defined render graph for testing.

```cpp
auto ForRenderGraphHarness(
    graphics::IGraphics& gfx,
    const RenderGraphConfig& config)
    -> std::unique_ptr<RenderGraphHarness>;
```

**Vortex adaptation:**

- Creates a Vortex `Renderer` and, when needed, a `SceneRenderer`
- Allows the caller to define a custom render graph
- Validates resource transitions and pass ordering

**Validation tasks (Phase 5E):**

1. Create graph harness with Vortex backend
2. Define multi-pass graph (depth → base → lighting → tonemap)
3. Execute and verify correct output

### 2.3 ForOffscreenScene

**Purpose:** Render a complete scene to an offscreen target.

```cpp
auto ForOffscreenScene(
    graphics::IGraphics& gfx,
    const OffscreenConfig& config)
    -> std::unique_ptr<OffscreenRenderer>;
```

**Vortex adaptation:**

- Creates a full Vortex rendering pipeline targeting an offscreen texture
- Supports both deferred and forward `ShadingMode` per config
- Used for thumbnails, material previews, scene screenshots

**Validation tasks (Phase 5E):**

1. Render scene offscreen in deferred mode → verify GBuffer + lighting
2. Render scene offscreen in forward mode → verify direct SceneColor
3. Read back offscreen texture → verify content matches expectations
4. Thumbnail scenario: render at reduced resolution → verify correct output

## 3. ShadingMode for Offscreen

### 3.1 Deferred Offscreen

Full deferred pipeline: GBuffer allocation, depth prepass, base pass,
deferred lighting, post-process. Full fidelity but higher GPU cost.

### 3.2 Forward Offscreen

Lightweight forward pipeline: no GBuffer allocation, BasePass writes
directly to SceneColor with forward lighting. Lower cost, suitable for
thumbnails and material previews.

### 3.3 Configuration

```cpp
struct OffscreenConfig {
  uint32_t width;
  uint32_t height;
  ShadingMode shading_mode{ShadingMode::kDeferred};
  bool enable_post_process{true};
  // Output target (must be pre-allocated)
  graphics::TextureHandle output_target;
};
```

## 4. Data Flow

### 4.1 Offscreen Renderer Lifecycle

```text
OffscreenRenderer::Render(scene)
  │
  ├─ Allocate SceneTextures (at config resolution)
  ├─ Create temporary CompositionView (single view)
  │
  ├─ SceneRenderer::OnFrameStart()
  ├─ SceneRenderer::OnPreRender()
  ├─ SceneRenderer::OnRender(ctx)
  │     └─ Full 23-stage dispatch (or forward-only subset)
  ├─ SceneRenderer::OnCompositing(ctx)
  │     └─ Produce composition submission targeting output_target
  ├─ SceneRenderer::OnFrameEnd()
  │
  └─ Return (output_target contains rendered result)
```

### 4.2 Resource Cleanup

All temporary resources (SceneTextures, per-view allocations) are released
after `Render()` returns. No persistent state between offscreen renders
unless the caller caches the `OffscreenRenderer` instance.

## 5. Interface Contracts

No new Vortex-specific classes. The facades are engine-level abstractions
that delegate to Vortex internals. Phase 5E validates that the delegation
works correctly.

### 5.1 Vortex Internal Support

The Vortex `Renderer` must support:

- Rendering to an arbitrary output target (not just the swap chain)
- Single-view rendering through the normal resolve / post / composition handoff
  with an offscreen target in place of swap-chain presentation
- Headless rendering (no window, no swap chain)

These are capabilities of the existing `Renderer` facade that must be
preserved during the Vortex migration.

## 6. Testability Approach

### 6.1 ForSinglePassHarness Tests

1. Create harness → execute depth pass → read back depth → verify values
2. Create harness → execute base pass → read back GBuffer → verify encoding
3. Create harness → execute tonemap → read back LDR → verify curve

### 6.2 ForRenderGraphHarness Tests

1. Create graph → execute multi-pass → verify final output

### 6.3 ForOffscreenScene Tests

1. Offscreen deferred → read back → verify lit scene
2. Offscreen forward → read back → verify lit scene (no GBuffer)
3. Thumbnail (256×256) → read back → verify correct downscaled output
4. Material preview → read back → verify material appearance

### 6.4 Feature-Gated Variant Validation (Phase 5F)

Per PRD §6.6, validate these variants:

- Depth-only renderer (only depth prepass, no BasePass/lighting)
- Shadow-only renderer (only shadow service active)
- No-environment renderer (no sky/fog/IBL)
- Diagnostics-only renderer (only diagnostics overlay)
- No-shadowing renderer (shadowing capability disabled)

Each variant must compile, run, and produce expected output (capability-
gated stages are null-safe no-ops).

## 7. Open Questions

1. **Headless renderer support:** Does the current D3D12 backend support
   rendering without a swap chain? This is needed for true offscreen
   rendering (server-side, CI tests). If not, Phase 5E documents the gap
   and a follow-up task is created.
