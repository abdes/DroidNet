# Exposure contract checkpoint

Status: slice-1 specification checkpoint recorded. Runtime implementation and
GPU acceptance remain open; specified limits are not yet native-qualified.

## Source and compiler evidence

Plan baseline: `04aa5e890`. Audited checkout:
`7c44dffa8f63183faf050598fcd38714de0ded36` (initially clean).

- ShaderBake `DxcShaderCompiler.cpp::MakeDxcArguments` and `CompileProfile.h`
  agree on `-Ges -enable-16bit-types -HV 2021`, Shader Model 6.6; Debug uses
  `-Od -Zi`, Release `-O3`. Neither requests denormal preservation or `-Gis`.
  `ActionKey.cpp` incorporates the fixed compiler-argument schema into cache keys.
- Bundled `packages/DXC/bin/x64/dxc.exe --version` reports
  `1.9.2602.17 (21d28f727)`. Direct compilation of the existing
  `VortexExposureAverageCS` succeeded under both profiles. Inspected DXIL
  disassembly has no `fp32-denorm-mode` attribute; no preservation guarantee
  may be inferred. Exposure/state math must use `float`, not `half`/min16float.
- CMake cache selects MSVC `cl`, Ninja Multi-Config, Debug `/Od /RTC1` and
  Release `/O2`; no `/fp:fast` override was found in the generated Debug rules
  or engine CMake configuration. CPU acceptance must still test coupled
  conversion overflow rather than assuming finite inputs imply a finite gain.
- The current fixed path floors gain; current auto shader floors luminance and
  gain and stores a 16-byte adapted-luminance/gain/EV/count record. Neither
  matches the new [state ABI](../../../lld/post-process-service.md#gpu-record-layouts).
- `SceneAsset.cpp::IsExpectedEnvironmentRecordSize` accepts only one exact
  size per known record. The current post-process record is 104 bytes; a new
  tail cannot be added without explicit old/new size dispatch and loader tests.

Primary compiler/format references:
[DXC denormal options](https://github.com/microsoft/DirectXShaderCompiler/wiki/Denorm-Mode),
[Direct3D floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules).
The latter establishes storage limits; it does not substitute for native D3D12
tests of the compiled shader on the target device.

## Numerical audit

`tools/vortex/audit_exposure_contract.py` uses independent Python arithmetic and
IEEE binary storage conversion. Ten checks pass: EV14/15/16, the physical
camera example, histogram uint32 headroom, FP32 storage boundaries, analytical
FP16 endpoint incompatibility, sampled P loss, FP32 endpoint preservation and
Earth-reference solar-disk headroom.
The report is `out/build-ninja/analysis/vortex/exposure-lightbench/contract-audit/arithmetic-audit.json`.
It includes checkout/source hashes and explicitly records `renderer_validation=false`.

Binary16 has less than 40 stops from its minimum positive subnormal to maximum
finite value, and less than 30 stops using only normals. The specified
[PBR domain](../../../../renderer-core/physically-based-rendering.md#hdr-domains-and-numerical-limits)
spans 56 stops; preserving both exact endpoints requires FP32. Required-signal
budgets, rather than every nonzero component, control actual format retention.
The 2026-09-16 contract adopts per-view eligibility, retained FP32 with
ordinary adaptation, stable return and bounded status using the existing modes.

## Owned contracts

| Contract                                                     | Owner                                                                                    |
| ------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| Calibration, hybrid response, numerical domain/error budgets | [PBR specification](../../../../renderer-core/physically-based-rendering.md)             |
| GPU state/frame/status records, lifecycle and ownership      | [PostProcessService](../../../lld/post-process-service.md)                               |
| Versioned packed fields and compatibility                    | [Environment authoring](../../../../../../../design/editor/lld/environment-authoring.md) |
| HDR product inventory and FP16 suitability                   | [SceneTextures](../../../lld/scene-textures.md)                                          |
| View ABI, ordering and reflection                            | [Shader contracts](../../../lld/shader-contracts.md)                                     |
| View-local settings, sharing and isolation                   | [MultiView](../../../lld/multi-view-composition.md)                                      |
| Experiment ownership and measurement semantics               | [LightBench](../../../../renderer-core/lightbench.md)                                    |

## Reproduction

From the engine root, run the arithmetic command in plan section 9. Compile the
existing shader for each profile (substitute `debug`/`release` output names):

```powershell
$audit = 'out/build-ninja/analysis/vortex/exposure-lightbench/contract-audit'
$shader = 'src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl'
./packages/DXC/bin/x64/dxc.exe -Ges -enable-16bit-types -HV 2021 -T cs_6_6 -E VortexExposureAverageCS -Od -Zi -I src/Oxygen -I src/Oxygen/Graphics/Direct3D12/Shaders -Fo "$audit/exposure-debug.dxil" -Fd "$audit/exposure-debug.pdb" -Fc "$audit/exposure-debug.ll" $shader
./packages/DXC/bin/x64/dxc.exe -Ges -enable-16bit-types -HV 2021 -T cs_6_6 -E VortexExposureAverageCS -O3 -I src/Oxygen -I src/Oxygen/Graphics/Direct3D12/Shaders -Fo "$audit/exposure-release.dxil" -Fc "$audit/exposure-release.ll" $shader
```

These commands compile existing source, not the planned replacement. Initial
compilation without `-I src/Oxygen` failed on the generated bindless include;
the commands above include the required root and passed.

## Checkpoint result and remaining runtime gates

The 2026-09-16 physical-camera scope includes
native aperture/shutter/ISO persistence, retaining the existing editor UI.
This promotes that part of `EV01-CAMERA-PHYSICAL-AUTHORING` into slice 6.

Accepted native extension, preserving the current editor Manual/Auto UI:
scene version 6 camera records retain their projection prefix, then append
float32 aperture_f, shutter_rate and iso. Perspective offsets 20/24/28 produce
32-byte records; orthographic offsets 28/32/36 produce 40-byte records. The user
overrode backward compatibility on 2026-09-21: migrate the entire repository to
v6 and reject older formats, retaining no legacy reader/hydration code. New
authoring defaults are 11/125/100. No DOF or motion-blur behavior is
implied. Camera asset implementation belongs to slice 6; the independent
runtime mathematics and GPU work does not depend on those storage changes.

Owning documents now specify the public behavior, exact GPU/asset layouts,
mathematical domain/error budgets and active HDR producer/consumer inventory.
The independent arithmetic audit passes 10/10; existing shader Debug/Release
compilation and disassembly inspection pass. File-link validation for the five
rewritten/new owning documents passes 21/21; `git diff --check` passes.

This closes the slice-1 specification gate. Slice 2 starts with the fixed-gain
regression. Native fixtures must qualify the specified domain, pre-store checks,
compiler behavior and tolerance budgets during the corresponding runtime gates.
No native image/capture or renderer acceptance is claimed by this checkpoint.
