# VTX-M04D.2 — UE5.7 exponential height fog parity

Status: `validated`

| Field     | Summary                                                                       |
| --------- | ----------------------------------------------------------------------------- |
| Outcome   | Analytic height fog, second layer, distance controls and atmosphere coupling. |
| Remaining | Extensions: [VX-FOG-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities).    |
| Evidence  | [Validation record](validation.md)                                            |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M04D.1

## Delivered scope

UE5.7-informed authored parameters, CPU/HLSL publication, analytic Stage-15 application, disabled fast path, focused runtime/capture proof, and city-scale capture proof. Cubemap inscattering resource binding/sampling remains explicitly deferred.

## Scope and acceptance

Required work:

- Implement UE5.7-grade authored parameter coverage for primary and secondary
  fog layers.
- Match the relevant `FogRendering` / `HeightFogCommon` algorithms for
  density, falloff, directional inscattering, deferred cubemap inscattering
  state, start/cut
  distance, max opacity, and sky-atmosphere coupling.
- Preserve disabled/no-fog fast paths without turning them into fake parity.
- Add shader tests or capture analysis that prove height fog modifies the
  expected pixels and respects sky/scene depth semantics.

Exit gate:

- `validated` on 2026-04-26.
- UE5.7 grounding covered `FogRendering.cpp`, `HeightFogCommon.ush`, and
  `ExponentialHeightFogComponent.cpp`.
- Focused EnvironmentLightingService and SceneRendererPublication tests pass.
- VortexBasic enabled and disabled RenderDoc proof validates Stage-15 fog
  presence/removal, captured fog static data, SceneColor contribution, and
  disabled fast path.
- City-scale RenderScene capture proof validates the `CityEnvironmentValidation`
  Stage-15 height-fog payload and SceneColor contribution.
- Cubemap inscattering resource binding/sampling remains explicitly deferred
  and is not part of the validated claim.

Status: `validated`

This plan covers only exponential height fog. It preserves the validated
VTX-M04D.1 publication-truth baseline and does not claim local fog,
volumetric fog, real SkyLight capture/filtering, async migration proof, or full
environment runtime closure.

## Scope

- Preserve current atmosphere, sky, below-horizon, SkyLight/IBL unavailable,
  and Stage 14 observability contracts from VTX-M04D.1.
- Implement UE5.7-informed height-fog authoring and runtime translation for:
  primary and secondary fog layers; density, falloff, and height offset;
  start, end, cutoff distance, and max opacity; fog inscattering luminance;
  directional inscattering luminance, exponent, and start distance;
  sky-atmosphere ambient contribution color scale; visibility flags already
  present in the model.
- Preserve explicit unavailable cubemap-resource behavior without implying
  usable cubemap fog. Runtime cubemap sampling is deferred until after the
  environment/fog runtime artifacts that affect current validation scenes are
  closed.
- Replace simplified height-fog shader/math behavior with a
  `FogRendering` / `HeightFogCommon`-shaped implementation.
- Keep far-depth sky pixels excluded from fog contribution.

## UE5.7 Grounding

Implementation and review must check:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\FogRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\HeightFogCommon.ush`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp`
- Sky-atmosphere files only where height fog consumes sky-atmosphere ambient
  contribution or sky-depth exclusion semantics.

## Implementation Steps

1. Audit existing Vortex height-fog model, state translation, CPU/HLSL static
   data, fog pass, and tests against the UE5.7 reference fields and shader
   equations.
2. Widen or correct CPU data translation only where gaps exist, keeping
   publication truth and invalid-resource states explicit.
3. Update `Fog.hlsl` and any required environment shader contract structs in
   lockstep with CPU layout changes.
4. Add focused EnvironmentLightingService tests for enabled and disabled
   height fog, authored parameter changes, secondary layer, directional
   inscattering, start/end/cutoff/max-opacity semantics, sky-depth exclusion,
   deferred cubemap unavailable behavior, and publication truth preservation.
5. Extend SceneRenderer publication tests only if the renderer boundary needs
   additional assertions beyond the VTX-M04D.1 Stage 14 baseline.
6. Record changed files, UE5.7 references checked, build/test results,
   shader-bake/catalog validation when contracts or shader behavior changed,
   doc/status scans, `git diff --check`, and residual gaps in
   `milestone README`.

## Proof Gate

This was the pre-validation gate for VTX-M04D.2: status had to remain
`in_progress` unless all required implementation existed, docs/status were
updated, and the following fresh evidence was recorded:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If CPU/HLSL layout or shader behavior changes, run the project shader
bake/catalog validation and record the exact command and result.

## Residual Gaps Not Closed By This Plan

- Cubemap inscattering authoring is translated into the GPU contract and
  published as authored, but Vortex does not yet have a bindable height-fog
  cubemap resource path. Runtime cubemap sampling remains explicitly
  unavailable, with `CubemapAuthored` set and `CubemapUsable` unset. This is a
  deferred nice-to-have, not the current priority for VTX-M04D closure. It must
  not block city-scale atmosphere/fog artifact work, aerial-perspective proof,
  local-fog proof, volumetric-fog proof, or environment runtime proof.
- Aerial perspective capture/reflection proof remains VTX-M04D.6 scope.
- Full environment runtime closure remains VTX-M04D.5 scope.
- Real SkyLight capture/filtering.
- Async runtime migration proof.
- DiagnosticsService implementation.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
