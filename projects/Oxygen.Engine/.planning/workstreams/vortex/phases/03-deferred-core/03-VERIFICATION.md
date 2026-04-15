---
phase: 03-deferred-core
verified: 2026-04-15T12:30:36.9647652Z
status: gaps_found
score: 4/6 must-haves verified
overrides_applied: 0
gaps:
  - truth: "Opaque rendering writes GBuffers by default through the Vortex base pass."
    status: failed
    reason: "Stage 9 is wired as a proof-oriented shell: it builds draw-command metadata and flips publication/velocity booleans, but it does not submit render work or write GBuffer attachments."
    artifacts:
      - path: "src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp"
        issue: "Execute() uses the prepared frame and velocity availability, then sets has_published_base_pass_products_ without any render-target write or shader dispatch."
      - path: "src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp"
        issue: "Produces sorted draw-command metadata only; no downstream consumer executes those commands in the renderer."
      - path: "src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp"
        issue: "Base-pass proofs validate publication flags/bindings, not actual GBuffer contents."
    missing:
      - "Wire Stage 9 to a real base-pass render path that consumes the generated draw commands and writes GBufferA-D plus SceneColor emissive."
      - "Add proof that validates written GBuffer content or emitted render work, not just binding promotion."
  - truth: "Deferred direct lighting reads GBuffers and produces a visually correct lit scene using fullscreen/stencil-bounded techniques."
    status: failed
    reason: "Stage 12 only records DeferredLightingState telemetry from published bindings and scene traversal; the deferred-light shader family is compiled but not invoked by the CPU path, and SceneColor is not mutated by lighting work."
    artifacts:
      - path: "src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp"
        issue: "RenderDeferredLighting() sets counters/flags such as accumulated_into_scene_color and used_stencil_bounded_local_lights instead of dispatching fullscreen/stencil passes."
      - path: "src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h"
        issue: "Deferred-light shaders are registered, but no C++ stage wiring references those shader requests."
      - path: "src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp"
        issue: "Deferred-light tests assert telemetry flags and consumed binding indices, not SceneColor texture output."
    missing:
      - "Wire Stage 12 CPU execution to the deferred-light shader family and actual fullscreen/stencil-bounded lighting passes."
      - "Add proof of SceneColor lighting output and shader-path execution instead of telemetry-only assertions."
deferred:
  - truth: "RenderDoc runtime validation of the deferred-core output"
    addressed_in: "Phase 4"
    evidence: "design/vortex/PLAN.md exit gate explicitly defers RenderDoc runtime validation to Phase 4, and roadmap Phase 4 goal covers real runtime migration of Async and end-to-end presentation validation."
---

# Phase 3: Deferred Core Verification Report

**Phase Goal:** minimal lit deferred scene with truthful Stage 2/3/9/10/12 contracts, automated closeout proof, and RenderDoc runtime validation explicitly deferred to Phase 04 per the revised phase plan.
**Verified:** 2026-04-15T12:30:36.9647652Z
**Status:** gaps_found
**Re-verification:** No

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Vortex shader contracts and entrypoints compile through `ShaderBake` and the engine shader catalog. | ✓ VERIFIED | `EngineShaderCatalog.h` registers depth/base/debug/deferred-light requests at [src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:287), [src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:320); `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4` exited `0`. |
| 2 | Stage 2 is a truthful InitViews contract with prepared-scene publication and active-view rebinding. | ✓ VERIFIED | Stage 2 dispatch/rebind is in [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:225); InitViews publishes prepared frames from ScenePrep in [src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:77); targeted publication/deferred-core tests passed. |
| 3 | Stage 3/9/10 publication, promotion, and velocity contracts are present and proof-backed. | ✓ VERIFIED | Stage 3/9/10 ordering is explicit in [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:237), [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:265), [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:279); publication/velocity proofs are in [src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp:348) and [src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp:437); targeted tests passed. |
| 4 | Automated Phase 3 closeout proof exists, passes, and explicitly defers runtime RenderDoc validation to Phase 4. | ✓ VERIFIED | The one-line runner in [tools/vortex/Verify-DeferredCoreCloseout.ps1](/F:/projects/DroidNet/projects/Oxygen.Engine/tools/vortex/Verify-DeferredCoreCloseout.ps1:33) executes capture/analyze/assert plus synthetic negative-path validation; the generated report at `out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt` recorded all seven keys as `pass`; Phase 4 deferral is encoded in [tools/vortex/Run-DeferredCoreFrame10Capture.ps1](/F:/projects/DroidNet/projects/Oxygen.Engine/tools/vortex/Run-DeferredCoreFrame10Capture.ps1:187) and [design/vortex/PLAN.md](/F:/projects/DroidNet/projects/Oxygen.Engine/design/vortex/PLAN.md:354). |
| 5 | Opaque rendering writes GBuffers by default through the Vortex base pass. | ✗ FAILED | `BasePassModule::Execute()` in [src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:25) only builds draw-command metadata and marks products published; `renderer_` is unused and there is no render-target write or shader dispatch. |
| 6 | Deferred direct lighting reads GBuffers and produces a lit scene using fullscreen/stencil-bounded techniques. | ✗ FAILED | `RenderDeferredLighting()` in [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:622) only records counters/flags such as `accumulated_into_scene_color` and `used_stencil_bounded_local_lights`; the deferred-light HLSL exists in the catalog, but no CPU stage wiring invokes it. |

**Score:** 4/6 truths verified

### Deferred Items

Items not yet met but explicitly addressed in later milestone phases.

| # | Item | Addressed In | Evidence |
| --- | --- | --- | --- |
| 1 | RenderDoc runtime validation of deferred-core output | Phase 4 | [design/vortex/PLAN.md](/F:/projects/DroidNet/projects/Oxygen.Engine/design/vortex/PLAN.md:354) defers runtime validation; roadmap Phase 4’s goal is real runtime migration and presentation validation. |

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli` | Mirror CPU scene-texture binding contract | ✓ VERIFIED | Field parity with [src/Oxygen/Vortex/SceneRenderer/SceneTextures.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneTextures.h:160) is present at [src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli:15). |
| `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli` | Mirror CPU view-frame binding contract | ✓ VERIFIED | `scene_texture_frame_slot` mirrors [src/Oxygen/Vortex/Types/ViewFrameBindings.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/Types/ViewFrameBindings.h:28) at [src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli:20). |
| `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp` | Real Stage 2 publication path | ✓ VERIFIED | Uses `BeginFrameCollection`, `PrepareView`, `FinalizeView`, and publishes `PreparedSceneFrame` at [src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:94). |
| `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp` | Real Stage 3 rendering path | ⚠️ HOLLOW | Uses prepared-frame metadata and marks completeness/published depth, but explicitly notes GPU command submission is not fully materialized at [src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp:49). |
| `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp` | Real Stage 9 GBuffer-writing path | ⚠️ HOLLOW | Uses `BasePassMeshProcessor`, then sets publication/velocity booleans; no render submission or texture mutation is present at [src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:25). |
| `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` | Real Stage 12 lighting path | ⚠️ HOLLOW | Stage 12 records telemetry only via `DeferredLightingState` in [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h:41) and [src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:648). |
| `tools/vortex/Verify-DeferredCoreCloseout.ps1` | Automated closeout runner with positive and negative-path proof | ✓ VERIFIED | Runs capture/analyze/assert, synthetic fail injection, and ledger checks at [tools/vortex/Verify-DeferredCoreCloseout.ps1](/F:/projects/DroidNet/projects/Oxygen.Engine/tools/vortex/Verify-DeferredCoreCloseout.ps1:33). |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| `SceneRenderer.cpp` | `InitViewsModule` | `init_views_->Execute(ctx, scene_textures_)` | WIRED | [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:228) and [InitViewsModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp:77). |
| `SceneRenderer.cpp` | `DepthPrepassModule` | `depth_prepass_->Execute(...)` + `ApplyStage3DepthPrepassState()` | WIRED | [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:243) and [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:252). |
| `SceneRenderer.cpp` | `BasePassModule` | `base_pass_->Execute(...)` + `ApplyStage10RebuildState()` | WIRED | [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:272) and [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:281). |
| `EngineShaderCatalog.h` | ShaderBake build | `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12` | WIRED | Catalog registrations at [EngineShaderCatalog.h](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h:287) and build step exit `0`. |
| `BasePassModule` | Actual GBuffer writes | Stage 9 execution | NOT_WIRED | `BasePassMeshProcessor` produces commands, but no renderer consumer submits them; `scene_textures`/`renderer_` are effectively unused for rendering in [BasePassModule.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp:27). |
| `SceneRenderer::RenderDeferredLighting` | Deferred-light shader family / SceneColor output | Stage 12 execution | NOT_WIRED | The CPU path sets telemetry flags in [SceneRenderer.cpp](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp:673); shader symbols only appear in HLSL and catalog files, not in Stage 12 C++ execution. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| --- | --- | --- | --- | --- |
| `InitViewsModule.cpp` | `prepared_frame` | `ScenePrepPipeline` via `BeginFrameCollection` / `PrepareView` / `FinalizeView` | Yes | ✓ FLOWING |
| `DepthPrepassModule.cpp` | `completeness_`, `has_published_depth_products_` | Local state after `BuildDrawCommands(...)` | No actual depth write proven | ⚠️ HOLLOW |
| `BasePassModule.cpp` | `has_published_base_pass_products_`, `has_completed_velocity_for_dynamic_geometry_` | Local booleans after `BuildDrawCommands(...)` | No actual GBuffer write proven | ⚠️ HOLLOW |
| `SceneRenderer.cpp` Stage 12 | `DeferredLightingState` | Published binding slots + scene traversal counts | No actual SceneColor mutation proven | ⚠️ HOLLOW |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Shader/catalog build is green | `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4` | Exit `0` | ✓ PASS |
| Deferred-core/publication proof surface runs | `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)'` | `2/2` passed | ✓ PASS |
| Full Vortex suite currently runs green | `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'` | `31/31` passed on rerun | ✓ PASS |
| One-line closeout runner validates positive + negative path | `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10` | Exit `0`; report validated; script printed an expected synthetic-failure line during the negative-path check | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| `SHDR-01` | `03-01`, `03-02`, `03-12` | Vortex shader contracts, entrypoints, and shared libraries compile through ShaderBake and engine shader catalog registration. | ✓ SATISFIED | Contract parity in HLSL files plus catalog registrations/build success. |
| `DEFR-01` | `03-03` through `03-11` | Opaque scene rendering writes GBuffers by default through the Vortex base pass. | ✗ BLOCKED | Stage 9/10 bindings and tests exist, but base pass does not submit rendering work or write GBuffers. |
| `DEFR-02` | `03-13` through `03-15` | Deferred direct lighting reads GBuffers using a correctness-first fullscreen/stencil-bounded path. | ✗ BLOCKED | Stage 12 telemetry exists, but no actual deferred-light pass or SceneColor lighting output is wired. |

No orphaned Phase 3 requirements were found in `REQUIREMENTS.md`; the workstream maps only `SHDR-01`, `DEFR-01`, and `DEFR-02` to Phase 3.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp` | 49 | Explicit note that GPU command submission is not fully materialized | 🛑 Blocker | Confirms Stage 3 is not a real rendering path yet. |
| `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp` | 27 | `scene_textures` not used for actual writes | 🛑 Blocker | Stage 9 can mark products published without writing GBuffers. |
| `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp` | 49 | `renderer_` unused | 🛑 Blocker | No render submission exists behind the base-pass shell. |
| `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` | 673 | `accumulated_into_scene_color = true;` flag assignment | 🛑 Blocker | Lighting success is inferred from telemetry, not SceneColor output. |
| `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` | 648 | `DeferredLightingAccumulatesIntoSceneColor` asserts a flag and bound UAV index only | ⚠️ Warning | Passing tests do not prove that lighting mutated SceneColor contents. |
| `tools/vortex/AnalyzeDeferredCoreCapture.py` | 232 | Closeout analyzer treats token presence plus step exit codes as proof of lit output | ⚠️ Warning | Automated closeout can pass without proving actual rendering work. |

### Gaps Summary

Phase 3 successfully landed the shader contract surface, stage-order wiring, proof tests, and the automated closeout scripts. Those artifacts prove that Stage 2/3/9/10/12 contracts exist and that the Phase 3 closeout runner works under the revised “source/test/log plus Phase 4 RenderDoc deferral” contract.

The phase goal itself is still not achieved. The current Stage 3/9/12 code remains proof-oriented rather than render-oriented: `DepthPrepassModule` explicitly acknowledges the GPU submission path is not fully materialized, `BasePassModule` only builds draw-command metadata and flips booleans, and `RenderDeferredLighting` only records telemetry from scene traversal. As a result, `SHDR-01` is satisfied, but `DEFR-01` and `DEFR-02` are not. RenderDoc runtime validation is correctly deferred to Phase 4 and is not counted as a gap here.

---

_Verified: 2026-04-15T12:30:36.9647652Z_
_Verifier: Codex (gsd-verifier)_
