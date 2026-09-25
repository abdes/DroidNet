# Slice F - Point-Light Conventional Shadows — recorded evidence

Validation evidence:

- Focused build/shader validation passed:
  `cmake --build out\build-ninja --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShadowService.Tests oxygen-graphics-direct3d12_shaders --parallel 4`.
- Focused tests passed: `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
  `43/43`; `Oxygen.Vortex.ShadowService.Tests` `9/9`.
- ShaderBake repacked `186` shader modules after the point-shadow HLSL change.
- CDB/D3D12 audit
  `point-shadow-validation.final.debug-layer.report.txt` passed with runtime
  exit `0`, no debugger break, `0` D3D12 errors, `0` DXGI errors, and `0`
  blocking warnings.
- RenderDoc probe `point-shadow-validation.final.point-shadow-probe.txt` passed:
  Stage 8 point shadow draws
  `168,171,187,190,206,209,225,228,244,247,263,266`; non-clear point-shadow
  cube-array slices `0`, `2`, and `5` with max depth about `0.69985/0.70000`;
  Stage 12 point draw `343`; Stage 12 bound
  `Vortex.PointShadowCubeSurface`; Stage 12 `SceneColor` changed with max
  `[839.5, 772.5, 688.5, 1.0]`.
- User visual validation on 2026-04-27 confirmed both the point-light shell bug
  and the point-shadow cube artifact were fixed.

## Validation and remaining work

**Qualification:** `validated`

Directional CSM UE5.7 audit/remediation is recorded in `shadow-service.md` and `VTX-M05D-conventional-shadow-parity.md`: stable/no-AA frusta, sphere bounds, texel snapping, 5000-unit directional depth extent, UE-style non-last transition overlap coverage, optional CSM constant/slope depth bias, default zero bias, and resolution-hint wiring are present. Release `RenderScene --scene VsmTwoCubes --directional-shadows conventional` smoke/capture/probes validated the local-scale CSM descriptor/settings after the Serio scene-loader fix. Slice E spot-light conventional shadows are implemented and validated with focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe `spot-shadow-validation.bias0.final.spot-shadow-probe.txt`, and manual visual confirmation after authored spot bias `0.0`. Slice F point-light conventional shadows are implemented with cube-array storage, six explicit face depth slices, Stage 12 point-shadow consumption, point proxy sphere winding regression coverage, and focused `PointShadowValidation` proof. Validation passed `cmake --build out\build-ninja --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShadowService.Tests oxygen-graphics-direct3d12_shaders --parallel 4`; tests passed SceneRendererDeferredCore `43/43` and ShadowService `9/9`; ShaderBake repacked `186` modules. CDB report `point-shadow-validation.final.debug-layer.report.txt` passed with runtime exit `0`, no debugger break, `0` D3D12/DXGI errors, and `0` blocking warnings. RenderDoc probe `point-shadow-validation.final.point-shadow-probe.txt` proves Stage 8 point draws `168,171,187,190,206,209,225,228,244,247,263,266`, non-clear `Vortex.PointShadowCubeSurface` slices `0`, `2`, and `5`, Stage 12 point draw `343`, Stage 12 point SRV binding, and `SceneColor` contribution. Manual visual confirmation on 2026-04-27 approved both point-light shell and point-shadow artifact fixes.

**Remaining work:** No open M05D closure gap. EX07 also qualified Stage 18 local-shadow consumption, local-map caching and compatible cross-view sharing; see [final acceptance](../exposure/EX07/EX07F/validation.md). Layered one-pass point cubemap rendering and VSM remain future work.

## Slice A - Design Scope And Truth Surface — checks

- `git diff --check`.

## Slice B - Directional CSM UE5.7 Parity Audit — checks

- Source-to-target mapping recorded in section 4.1.
- Validation: source audit only.

## Slice C - City-Scale Instability Reproduction — checks

- Runtime city observation that the reported malformed right-frustum shadows are
  explained by the depth-range audit finding.
- Smaller-scene RenderDoc/debug-layer proof after remediation.

## Slice D - Directional CSM Remediation — checks

- Focused build/tests.
- ShaderBake/catalog validation if shadow shaders or ABI change.
- CDB/D3D12 debug-layer audit.
- RenderDoc proof showing Stage 8 cascade writes, Stage 12 consumption, stable
  projected shadows under camera movement, and expected debug-mask behavior.
- Manual visual confirmation for the city-scale scenario.

## Slice E - Spot-Light Conventional Shadows — checks

- Focused tests for publication and shader ABI.
- RenderDoc/CDB proof. Diagnostic evidence before the final bias/depth
  remediation:
  `spot-shadow-validation.bias01.spot-shadow-probe.txt` showed Stage 8 issuing
  two caster draws while `Vortex.SpotShadowSurface` remained all clear depth
  (`min=max=0`), and Stage 12 bound the spot shadow SRV correctly. Post-fix
  proof `spot-shadow-validation.bias0.final.spot-shadow-probe.txt` shows
  Stage 8 spot draws `168,171`, non-clear `Vortex.SpotShadowSurface` depth
  (`max=0.463512063`, center `0.447184265`), Stage 12 spot draw `248`, and
  Stage 12 binding `Vortex.SpotShadowSurface`.
- Manual visual confirmation: on 2026-04-27 the validation scene showed visible
  spot shadows after the spot-axis depth fix, and manual checks confirmed the shadows
  were perfect after setting the authored spot shadow bias to `0.0` and
  recooking `SpotShadowValidation`.
- Focused validation: `cmake --build out\build-ninja --target
Oxygen.Vortex.ShadowService.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests
Oxygen.Vortex.LightingService.Tests
Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests
oxygen-graphics-direct3d12_shaders --parallel 4` succeeded; test executables
  passed ShadowService `8/8`, SceneRendererDeferredCore `41/41`,
  LightingService `4/4`, and ShaderBakeCatalog `4/4`.
- CDB/D3D12 audit:
  `spot-shadow-validation.bias0.final.debug-layer.report.txt` passed with
  runtime exit `0`, no debugger break, `0` D3D12 errors, `0` DXGI errors,
  `0` blocking warnings, and one accepted DXGI live-factory shutdown warning.
