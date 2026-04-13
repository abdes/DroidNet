# Migration Playbook LLD

**Phase:** 4E — First Migration
**Deliverable:** D.13
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

Step-by-step playbook for migrating `Examples/Async` from the legacy
`Oxygen.Renderer` to `Oxygen.Vortex`. This is the PRD §6.1.1 success gate:
the migrated example must produce correct visual output matching the legacy
reference, with no long-lived compatibility clutter.

### 1.2 Why Examples/Async

`Examples/Async` is the designated first-migration target because it
exercises the core rendering path (scene setup, frame dispatch, composition)
without requiring advanced features (translucency, occlusion, multi-view).
Success here validates that the four migration-critical services (Lighting,
PostProcess, Shadows, Environment) are operational.

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) — runtime seam ownership, composition,
  resolve, extraction, and handoff boundaries
- PRD §6.1.1 — first migration exit criterion
- [PLAN.md §6](../PLAN.md) — Phase 4E step plan

## 2. Pre-Migration Baseline

### 2.1 Legacy Visual Baseline Capture

Before any migration work:

1. Build and run `Examples/Async` against legacy `Oxygen.Renderer`.
2. Capture RenderDoc frame at frame 10 (repo convention).
3. Save baseline screenshots and texture inspections:
   - Final back buffer (tonemapped output)
   - Depth buffer content
   - Any intermediate render targets visible in RenderDoc
4. Document observable behaviors:
   - Async operation workflow (what async tasks execute, when they complete)
   - Frame timing characteristics
   - Input handling behavior

### 2.2 Baseline Artifacts

| Artifact | Format | Purpose |
| -------- | ------ | ------- |
| `baseline_frame10.png` | Screenshot | Visual reference |
| `baseline_depth.png` | Depth visualization | Depth accuracy reference |
| `baseline_renderdoc.rdc` | RenderDoc capture | Full pass inspection |
| `baseline_behaviors.md` | Document | Observable behavior checklist |

## 3. Migration Steps

### 3.1 Step 1: Header and Namespace Changes

Replace renderer includes and namespaces:

| Legacy | Vortex |
| ------ | ------ |
| `#include "Oxygen/Renderer/..."` | `#include "Oxygen/Vortex/..."` |
| `oxygen::renderer::` | `oxygen::vortex::` |
| `OXGN_RNDR_API` | `OXGN_VRTX_API` |

The example code should use the public API only:

- `Renderer` (Vortex facade)
- `CompositionView` (intent API)
- Scene setup APIs

### 3.2 Step 2: Renderer Initialization

Replace legacy renderer creation with Vortex:

```cpp
// Legacy:
auto renderer = oxygen::renderer::CreateRenderer(gfx, config);

// Vortex:
auto renderer = oxygen::vortex::CreateRenderer(gfx, config);
```

The `RendererConfig` structure should carry over. Verify that config fields
map correctly between legacy and Vortex.

This step is not a cosmetic constructor swap. Before treating the migration as
viable, verify that the Vortex bootstrap path reaches the real
`SceneRenderBuilder` / `SceneRenderer` runtime seam and that Renderer Core owns
view assembly, publication, composition planning, target resolution, and
compositing execution per the architecture.

### 3.3 Step 3: Scene Setup

Verify scene setup code works unchanged:

- Scene graph construction (nodes, transforms, geometry, materials)
- Light setup (directional, point, spot)
- Camera setup via `CompositionView`

Most scene setup code should be engine-level (not renderer-specific). If
the example directly calls renderer internals, those calls must be
refactored to use the Vortex public API.

### 3.4 Step 4: Frame Loop Integration

Verify the frame loop dispatch works:

```cpp
// Both legacy and Vortex should support this pattern:
renderer.OnFrameStart(frame);
renderer.OnPreRender(frame);
renderer.OnRender(ctx);
renderer.OnCompositing(ctx);
renderer.OnFrameEnd(frame);
```

The Vortex renderer delegates to `SceneRenderer` internally. The external
frame loop API should be compatible.

Compatibility of the outer loop is necessary but not sufficient. The migrated
example must prove that composition submission, resolve behavior, and handoff
artifacts travel through the real Vortex runtime seams rather than through an
example-local shortcut.

### 3.5 Step 5: Async Operation Validation

`Examples/Async` exercises asynchronous operations:

- Verify async tasks complete correctly under Vortex
- Verify no dead-locks or race conditions in the new dispatch path
- Verify observable async behavior matches baseline

### 3.6 Step 6: Non-Runtime Facade Validation

Validate the two non-runtime facades against Vortex:

1. `ForSinglePassHarness()` — single pass, validated context
2. `ForRenderGraphHarness()` — render graph, validated context

These facades must produce valid output when backed by Vortex.

## 4. Visual Parity Validation

### 4.1 RenderDoc Comparison

1. Capture Vortex frame at frame 10.
2. Compare against baseline:
   - **Back buffer:** Pixel-level comparison. Expect near-identical output.
     Minor floating-point differences acceptable (< 1/255 per channel).
   - **Stage-family visibility:** Verify the active stage families are
     discoverable by stable names and appear in the correct relative order.
     Bootstrap work may appear outside the main stage list, and resolve /
     extraction only appear when active.
   - **GBuffer inspection:** Verify GBufferA–D contain valid data matching
     scene geometry.

### 4.2 Acceptance Criteria

| Criterion | Metric | Threshold |
| --------- | ------ | --------- |
| Visual match | PSNR or perceptual diff | > 40 dB (near-identical) |
| Depth accuracy | Depth buffer comparison | < 0.001 max error |
| Frame timing | Average frame time | Informational only unless product leadership promotes it to a hard gate |
| Async behavior | Observable behavior match | All behaviors documented in baseline |
| No compatibility clutter | Code inspection | Zero legacy compatibility shims |

### 4.3 Known Acceptable Differences

- Tone mapping curve may differ slightly (ACES vs legacy operator)
- Shadow filtering quality may differ (implementation detail)
- Bloom intensity may differ (tuning parameter)

These are acceptable as long as the overall visual impression matches.

## 5. Behavior Parity Validation

### 5.1 Checklist

- [ ] Application starts and renders first frame
- [ ] Async operations execute and complete
- [ ] Camera controls work correctly
- [ ] Scene updates reflect in rendered output
- [ ] Window resize triggers correct resolution change
- [ ] Application shuts down cleanly (no leaks, no crashes)
- [ ] RenderDoc capture at frame 10 succeeds

### 5.2 Error Conditions

- [ ] GPU device lost → graceful recovery or clean error
- [ ] Invalid scene state → no crash, diagnostic log
- [ ] Missing assets → fallback behavior, no crash

## 6. Post-Migration Cleanup

### 6.1 Zero Compatibility Clutter Rule

Per PLAN.md, the migration must leave zero long-lived compatibility shims:

- No `#ifdef LEGACY_RENDERER` conditionals
- No parallel code paths for legacy vs Vortex
- No adapter layers wrapping legacy APIs
- No "temporary" workarounds that persist

### 6.2 Code Review Checklist

- [ ] All includes point to `Oxygen/Vortex/`
- [ ] All namespaces use `oxygen::vortex::`
- [ ] No references to legacy `Oxygen.Renderer` types
- [ ] Public API usage only (no internal headers)
- [ ] Build succeeds with only Vortex dependency

## 7. Composition and Presentation Validation (Phase 4F)

The migration already requires the real runtime composition path to be live.
Phase 4F therefore deepens proof of the surrounding artifacts rather than
introducing composition for the first time.

After visual/behavior parity is confirmed:

1. Validate single-view composition to screen under explicit inspection
2. Validate `ResolveSceneColor` end-to-end when resolve work is actually needed
3. Validate `PostRenderCleanup` extraction/handoff (SceneTextureExtracts
   correctly populated, no GPU resource leaks)

## 8. Testability Approach

1. **Automated regression:** Screenshot comparison test at frame 10 against
   baseline. Fail if PSNR < 40 dB.
2. **Manual validation:** Developer visual inspection of RenderDoc capture.
3. **Behavior test:** Run async workflow → verify completion callbacks fire.
4. **Leak test:** PIX/DXGI debug layer enabled, verify zero resource leaks
   at shutdown.

## 9. Open Questions

1. **Frame timing parity:** Vortex deferred path may be faster or slower
   than legacy forward path depending on scene complexity. Performance
   comparison is informational by default; do not silently treat it as a
   blocking product gate unless that decision is elevated explicitly.
