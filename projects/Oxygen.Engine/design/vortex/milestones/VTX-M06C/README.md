# VTX-M06C — Feature-gated runtime variants

Status: `validated`

| Field     | Summary                                                              |
| --------- | -------------------------------------------------------------------- |
| Outcome   | Reduced feature profiles and enabled/disabled-product qualification. |
| Remaining | None in the recorded scope.                                          |
| Evidence  | [Validation record](validation.md)                                   |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M06A, VTX-M06B

## Delivered scope

Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only variants with focused tests, CDB/debug-layer proof, RenderDoc scripted analysis, allocation-churn proof, and manual visual confirmation.

## Scope and acceptance

Scope:

- Depth-only rendering.
- Shadow-only rendering.
- No-environment variant.
- No-shadowing variant.
- No-volumetrics variant.
- Diagnostics-only overlay variant.
- Detailed implementation plan:
  [`plan/VTX-M06C-feature-gated-runtime-variants.md`](README.md).

Exit gate:

- Each variant compiles, runs, and validates the expected absence/presence of
  subsystem products without crashes or fake publications.

Status: `validated`

## 1. Goal

VTX-M06C proves that Vortex can assemble and run intentionally reduced
runtime variants without crashes, fake publications, stale products, or
downstream consumers reading disabled subsystem data.

The milestone builds on the validated multi-view and offscreen surfaces from
VTX-M06A/B. The target is a production-clean feature gate contract for
runtime views and offscreen scene captures, not demo-only shortcuts.

## 2. Scope

In scope:

- Depth-only rendering: depth prepass product exists; base pass, lighting,
  environment, translucency, post-process, and composition color products are
  either omitted or explicitly invalid for the variant contract.
- Shadow-only rendering: conventional shadow products are generated for the
  requested view/light set without main-view color shading.
- No-environment rendering: sky, atmosphere, height fog, local fog, IBL, and
  environment frame products are disabled or invalid while opaque lighting,
  shadows, translucency, post, and composition remain valid.
- No-shadowing rendering: direct lighting and environment paths run without
  publishing or consuming shadow products.
- No-volumetrics rendering: sky/atmosphere/fog paths may run, but volumetric
  fog/local-fog volumetric products are disabled and not consumed.
- Diagnostics-only overlays: diagnostics/overlay output can be produced without
  scene lighting products.
- Variant input vocabulary on Vortex-native surfaces, capability gates,
  per-view feature masks, null-safe service behavior, diagnostics records, and
  proof tooling.
- Focused tests and runtime proof showing both enabled and disabled product
  states for every variant.

Out of scope:

- New renderer architecture, new render graph/RDG infrastructure, or a second
  production scene renderer.
- Legacy `Oxygen.Renderer` references, fallbacks, or simplification paths.
- Future families such as VSM, IBL cubemap capture/filtering, GI/reflections,
  heterogeneous volumes, clouds, water, hair, and distortion.
- Full editor UI for configuring variants. Demo-app proof controls are allowed
  when they are clean runtime inputs.

## 4. Existing Behavior To Preserve

- Validated M06A multi-view behavior: per-view state handles, serialized
  view-family execution, scene-texture lease pool, data-driven composition,
  auxiliary dependency proof, overlay lanes, allocation-churn proof, and
  GroundGrid stability.
- Validated M06B offscreen behavior: deferred and solid-forward offscreen
  products, downstream texture composition, final shader-resource state, CDB
  proof, RenderDoc proof, and allocation-churn proof.
- Validated M05A-M05D behavior: diagnostics, occlusion, translucency, and
  conventional directional/spot/point shadowing.
- Vortex-native architecture only. No legacy renderer use.
- Truthful product publication: disabled stages must not leave valid-looking
  descriptors, revisions, or diagnostics products.

## 5. UE5.7 Parity References

VTX-M06C is feature-gating parity, not a new shading model. Local UE5.7
grounding for the gate contract:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\WorldPartition\WorldPartitionMiniMapHelper.cpp`
  uses a scene-capture component with show flags disabling lighting,
  atmosphere, post processing, fog, volumetric fog, dynamic shadows, and sky
  lighting for a reduced runtime capture.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  branches the renderer on output mode, including a depth-prepass-only
  `ERendererOutput::DepthPrepassOnly` path that copies depth/capture output
  without continuing through the full final-scene-color path.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  gates deferred lighting on `EngineShowFlags.Lighting`,
  `EngineShowFlags.DeferredLighting`, GBuffer use, and ray-traced overlay
  state.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  gates sky atmosphere, volumetric fog, local fog volumes, volumetric clouds,
  and dynamic shadows through view-family show flags and renderer-output
  predicates.

Vortex should mirror the discipline: feature gates are input contracts that
select real stage behavior and product validity. They are not post-hoc analyzer
exceptions.

## 6. Contract Truth Table

| Variant          | Required products                                                                       | Disabled products                                                                             | Consumers that must be safe                                                             |
| ---------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Depth-only       | Prepared view, SceneDepth, optional depth diagnostics.                                  | SceneColor, GBuffer, lighting, shadows, environment, translucency, post-process color output. | Resolve/post/composition paths must not require SceneColor for the depth-only contract. |
| Shadow-only      | Prepared view, light selection, shadow frame bindings/surfaces for eligible lights.     | Main SceneColor/GBuffer/lighting/environment/post products.                                   | Shadow service must not depend on base-pass color products.                             |
| No-environment   | Prepared view, depth, GBuffer/SceneColor, lighting, optional shadows, post/composition. | Environment frame bindings, sky/atmosphere/fog/IBL, volumetric/local-fog products.            | Lighting/translucency must see invalid environment slots and remain stable.             |
| No-shadowing     | Prepared view, depth, GBuffer/SceneColor, lighting, environment/post as requested.      | Shadow frame bindings and shadow surfaces.                                                    | Lighting, environment, and translucency must not sample stale shadow descriptors.       |
| No-volumetrics   | Environment bindings may exist for sky/height fog; non-volumetric sky/fog may render.   | Integrated light scattering and volumetric fog/local-fog volumetric products.                 | Stage 15 and translucency must treat volumetric slots as invalid.                       |
| Diagnostics-only | Diagnostics pass/product records and overlay/composition output if requested.           | Scene lighting, shadows, environment, translucency, post products unless explicitly enabled.  | Diagnostics overlay path must not fabricate scene products.                             |

Every disabled state must be represented by invalid shader-visible indices,
`valid=false` diagnostics products, omitted RenderDoc pass scopes, or explicit
disabled diagnostics records.

## 7. Implementation Slices

### A. Plan and Status Truth

Required work:

- Create this detailed plan.
- Update `PLAN.md`, `milestone README`, and the plan package README.
- During execution, keep VTX-M06C unvalidated until implementation and closure
  proof exist; the final status ledger now records that closure proof.

[Checks](validation.md#a-plan-and-status-truth--checks).

[Results](validation.md#a-plan-and-status-truth--results).

### B. Variant Vocabulary and Validation

Required work:

- Add a typed runtime variant/profile vocabulary that can represent all six
  VTX-M06C variants without demo-only flags or cross-domain option leakage.
- Map profiles to `ViewFeatureMask`, capability requirements, render/shading
  settings, and product expectations.
- Extend offscreen inputs and runtime view materialization so both runtime and
  offscreen proof paths use the same production contract.
- Add focused tests for profile defaults, invalid combinations, and capability
  requirement reporting.

[Checks](validation.md#b-variant-vocabulary-and-validation--checks).

[Results](validation.md#b-variant-vocabulary-and-validation--results).

### C. Depth-Only and Shadow-Only Stage Gates

Required work:

- Route depth-only views through preparation and depth products while bypassing
  color shading and consumers that require SceneColor.
- Route shadow-only views through light selection and shadow service products
  without requiring main-view base-pass color products.
- Publish explicit disabled or invalid state for omitted products.
- Add SceneRenderer tests for stage omission and product validity.

[Checks](validation.md#c-depth-only-and-shadow-only-stage-gates--checks).

[Results](validation.md#c-depth-only-and-shadow-only-stage-gates--results).

### D. Environment, Shadowing, and Volumetric Disable Gates

Required work:

- Implement no-environment, no-shadowing, and no-volumetrics gates at the
  per-view stage level.
- Ensure LightingService, EnvironmentLightingService, TranslucencyModule, and
  post-process consumers receive invalid slots rather than stale descriptors.
- Preserve normal validated behavior when the corresponding features are
  enabled.

[Checks](validation.md#d-environment-shadowing-and-volumetric-disable-gates--checks).

[Results](validation.md#d-environment-shadowing-and-volumetric-disable-gates--results).

### E. Diagnostics-Only and Overlay Variant

Required work:

- Support a diagnostics-only proof view that can publish diagnostics/overlay
  output without scene-lighting products.
- Keep diagnostics records truthful for omitted passes and invalid products.
- Add tests that fail if diagnostics-only creates fake scene products.

[Checks](validation.md#e-diagnostics-only-and-overlay-variant--checks).

[Results](validation.md#e-diagnostics-only-and-overlay-variant--results).

### F. Runtime Proof Layout and Analyzer

Required work:

- Add a visually inspectable Vortex demo proof layout for all six variants.
- Reuse production runtime/offscreen inputs; keep proof-specific scenario setup
  in demo/tooling code.
- Extend or add RenderDoc analysis to prove expected pass presence/absence,
  product publication, disabled-product state, and downstream consumption.
- Add a wrapper that runs build, CDB/debug-layer audit, RenderDoc capture,
  scripted assertions, and 60-frame allocation-churn proof.

[Checks](validation.md#f-runtime-proof-layout-and-analyzer--checks).

[Results](validation.md#f-runtime-proof-layout-and-analyzer--results).

### G. Closure and Ledger Update

Required work:

- Run the full focused suite and runtime proof.
- Run `git diff --check`.
- Update this plan and `milestone README` only with proven evidence.
- Keep residual gaps explicit. Do not mark VTX-M06C `validated` without
  implementation, focused tests, CDB/debug-layer proof, RenderDoc scripted
  analysis, allocation-churn proof, visual confirmation for the visual layout,
  and recorded residual gaps.

[Results](validation.md#g-closure-and-ledger-update--results).

## 10. Exit Gate

VTX-M06C cannot be marked `validated` until:

1. Implementation exists in production Vortex code.
2. Required plan/status/LLD updates are current.
3. Focused tests pass.
4. CDB/debug-layer proof passes.
5. RenderDoc scripted analysis passes for every variant.
6. Allocation-churn proof passes.
7. ShaderBake/catalog validation is recorded if shader/ABI changed.
8. Visual confirmation is recorded for the proof layout.
9. Residual gaps are recorded and accepted, or there are no residual gaps.

## 11. Replan Triggers

Revisit the design if:

- A requested variant cannot be represented by the current capability or
  per-view feature model without leaking test/demo state into production.
- A disabled stage still requires valid descriptors for downstream execution.
- Depth-only or shadow-only output requires a new product handoff contract not
  covered by the current offscreen/runtime composition model.
- UE5.7 grounding reveals that the chosen feature gate maps to a different
  renderer-output class than this plan assumes.
- Validation requires shader ABI or root-binding changes not captured by the
  current slice plan.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
