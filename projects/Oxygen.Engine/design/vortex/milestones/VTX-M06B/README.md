# VTX-M06B — Offscreen proof closeout

Status: `validated`

| Field     | Summary                                                                        |
| --------- | ------------------------------------------------------------------------------ |
| Outcome   | Deferred/forward offscreen products and their composition into visible views.  |
| Remaining | Extensions: [VX-OFFSCREEN-01](../../OPEN_ITEMS.md#p2--engineering-follow-ups). |
| Evidence  | [Validation record](validation.md)                                             |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M05A, VTX-M05C

## Delivered scope

`ForOffscreenScene` deferred/forward validation and preview/capture scenarios are implemented and validated. Closure proof covers focused tests, CDB/debug-layer audit, RenderDoc scripted analysis, non-empty offscreen product proof, downstream texture composition, 60-frame allocation-churn proof, and manual visual confirmation.

## Scope and acceptance

Scope:

- `ForOffscreenScene` facade validation for scene-derived offscreen rendering.
- Deferred and solid forward-mode permutations.
- Preview/thumbnail/capture use cases.
- Detailed implementation plan:
  [`plan/VTX-M06B-offscreen-proof-closeout.md`](README.md).

Status: `validated`

## 1. Goal

VTX-M06B proves that `Renderer::ForOffscreenScene()` renders scene-derived
Vortex content into caller-owned offscreen framebuffers without a swap chain,
and that the resulting product is usable by downstream Vortex-native
consumers.

The target is not a validation-only facade. The facade must execute the
existing Vortex scene renderer path against an offscreen target, cover deferred
and solid forward scene permutations, and leave a truthful product/final-state
contract for preview, thumbnail, and capture callers.

## 2. Scope

In scope:

- `ForOffscreenScene()` input validation for frame session, scene, view/camera,
  output target, and pipeline settings.
- Real `ValidatedOffscreenSceneSession::Execute()` rendering through Vortex
  frame/view execution, not a no-op or legacy renderer fallback.
- Deferred and solid forward-mode selection through offscreen pipeline settings.
- Output-target ownership and final state suitable for texture consumers.
- Focused unit/integration tests that fail if execution silently skips scene
  rendering.
- Runtime proof covering preview and capture/thumbnail style offscreen targets.
- CDB/debug-layer proof, RenderDoc scripted analysis, readback/product proof,
  and allocation-churn proof for repeated offscreen renders.
- A forward wireframe/debug offscreen product is useful regression coverage,
  but it is not accepted as the forward scene-product closure gate.
- Ledger updates only when the matching implementation and validation evidence
  exists.

Out of scope:

- Feature-gated variants such as depth-only, no-environment, no-shadows, and
  diagnostics-only. Those remain `VTX-M06C`.
- New render graph/RDG infrastructure.
- Legacy `Oxygen.Renderer` fallback paths.
- Full material-preview UI. VTX-M06B proves the rendering/product contract that
  such a UI can consume.

## 4. UE5.7 References

VTX-M06B is a Vortex facade/product milestone. UE5.7 grounding is required
where the implementation touches scene capture, thumbnail, view family, or
custom render target behavior:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Classes\Engine\SceneCapture.h`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\SceneCaptureRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\RendererModule.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`

The proof target is Vortex-native behavior, not line-for-line scene-capture
feature parity.

## 5. Implementation Slices

### A. Plan and Status Truth

Required work:

- Create this detailed plan.
- Update `PLAN.md`, `milestone README`, and the offscreen LLD status
  note so the milestone is `in_progress`, not `planned`.
- Record that current execution is validation-only/no-op and therefore not
  validated.

[Checks](validation.md#a-plan-and-status-truth--checks).

[Results](validation.md#a-plan-and-status-truth--results).

### B. Offscreen Frame Execution Substrate

Required work:

- Resolve the requested offscreen camera/view through the Vortex scene-view
  resolver.
- Build a one-view render context using a valid published view id and
  offscreen output target.
- Execute the existing Vortex scene renderer frame path against that context.
- Ensure output framebuffer usage is explicit and no swap-chain/present path is
  required.

[Checks](validation.md#b-offscreen-frame-execution-substrate--checks).

[Results](validation.md#b-offscreen-frame-execution-substrate--results).

### C. Deferred and Forward Pipeline Selection

Required work:

- Add typed offscreen pipeline settings for deferred and forward modes.
- Route the selected mode through per-view render settings.
- Validate that invalid or unsupported combinations fail before GPU work.
- Keep the distinction explicit: API routing proof is not solid forward
  rendering proof.

[Checks](validation.md#c-deferred-and-forward-pipeline-selection--checks).

[Results](validation.md#c-deferred-and-forward-pipeline-selection--results).

### D. Product Handoff and Final State

Required work:

- Ensure the caller-owned output target receives the composed scene result.
- Leave the offscreen product in a texture-consumer state rather than a present
  state.
- Add explicit diagnostics for missing or failed handoff.

[Checks](validation.md#d-product-handoff-and-final-state--checks).

[Results](validation.md#d-product-handoff-and-final-state--results).

### E. Runtime Preview and Capture Proof

Required work:

- Add or extend a Vortex-native runtime proof path that renders at least one
  preview-sized deferred offscreen target and one capture/thumbnail-style solid
  forward offscreen target.
- Display the same offscreen products inside a normal Vortex demo view so the
  proof is inspectable by eye as well as by capture analysis.
- Add scripted RenderDoc analysis for offscreen pass labels, output resource,
  draw/dispatch presence, and downstream texture consumption.
- Add readback or analyzer proof that both output products are non-empty and
  isolated from swap-chain presentation.

[Checks](validation.md#e-runtime-preview-and-capture-proof--checks).

[Results](validation.md#e-runtime-preview-and-capture-proof--results).

[Results](validation.md#e-runtime-preview-and-capture-proof--results-2).

### F. Closure and Ledger Update

Required work:

- Run the full focused unit/integration suite for this milestone.
- Run `git diff --check`.
- Update `milestone README` only with proven implementation and
  validation evidence.
- Keep residual gaps explicit. Do not mark `validated` without runtime proof,
  CDB/debug-layer proof, RenderDoc analysis, allocation-churn proof, and
  truthful final-state/product evidence.

[Results](validation.md#f-closure-and-ledger-update--results).

Exit gate:

- Implementation exists.
- Docs/status are current.
- Focused tests pass.
- CDB/debug-layer audit passes.
- RenderDoc scripted analysis passes.
- Repeated offscreen renders show no steady-state allocation churn.
- Offscreen output is proven usable by a downstream Vortex-native consumer.
- Both deferred and solid forward offscreen scene products are proven. Forward
  wireframe/debug proof may remain as regression coverage but cannot satisfy
  the solid forward gate.
- Remaining gaps are recorded as none for VTX-M06B.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
