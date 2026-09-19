# Exposure architecture references

Status: `reference`

Updated: 2026-09-20

The [exposure and LightBench implementation plan](../plan/exposure-and-lightbench-correction.md)
owns requirements, algorithms, delivery order and acceptance. LightBench is the
visual exposure benchmark; native MultiView visual/exposure correctness is a
mandatory delivery gate.

## 1. Canonical contracts

| Subject | Owner |
| --- | --- |
| Exposure equations and hybrid response | [PBR specification](../../renderer-core/physically-based-rendering.md); runtime state/layouts in [PostProcessService LLD](post-process-service.md) |
| View identity, transitions, sharing and HDR domains | [Execution plan section 4](../plan/exposure-and-lightbench-correction.md#4-runtime-lifecycle-and-hdr-domains); [multiview LLD](multi-view-composition.md) owns the public view boundary |
| Histogram, masks, curves and authoring | [Execution plan section 5](../plan/exposure-and-lightbench-correction.md#5-robust-metering-and-authoring) |
| Physical-light reference | [Execution plan section 6](../plan/exposure-and-lightbench-correction.md#6-calibrated-light-and-material-reference); [PBR specification](../../renderer-core/physically-based-rendering.md) owns engine units |
| LightBench and MultiView behavior | [Execution plan section 7](../plan/exposure-and-lightbench-correction.md#7-lightbench-benchmark-and-multiview-visual-qualification) |
| Measurements | [DiagnosticsService LLD](diagnostics-service.md), with experiment semantics owned by LightBench |
| Compiler/format checkpoint | [Audit report](../plan/exposure-contract-checkpoint.md), [HDR inventory](scene-textures.md#exposure-hdr-domain-and-format-inventory), per-view eligibility independent of gain validity |
| Performance controls and execution | [EX051 scopes and benchmark matrix](../IMPLEMENTATION_STATUS.md#321-slice-51-performance-qualification-and-correction); [FP32-only control](post-process-service.md#slice-51-fp32-baseline-and-precision-policy) |
| Fallback allocation ownership | [EX051-10A SceneColor lease contract](scene-textures.md#ex051-10a-independent-scenecolor-fallback-ownership) |
| Sequence and tests | [Implementation slices](../plan/exposure-and-lightbench-correction.md#8-ordered-implementation-slices) and [acceptance matrix](../plan/exposure-and-lightbench-correction.md#9-acceptance-matrix-and-execution) |

## 2. UE5.7 source map

Reference root: `F:/Epic Games/UE_5.7/Engine`.

| Source / symbol | Relevant behavior |
| --- | --- |
| `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp::GetEyeAdaptationParameters` | Ranges, compensation, force-target and speed parameters |
| `PostProcessEyeAdaptation.cpp::LuminanceMaxFromLensAttenuation` | Scene-luminance normalization |
| `PostProcessEyeAdaptation.cpp::AddHistogramEyeAdaptationPass` | Owner/borrower handling and buffer publication |
| `PostProcessEyeAdaptation.cpp::FViewInfo::UpdatePreExposure` | Per-view pre-exposure and final-composition consistency |
| `Source/Runtime/Renderer/Private/SceneRendering.cpp::FViewInfo::ShouldUpdateEyeAdaptationBuffer` | Owner updates and borrower bootstrap exception |
| `Shaders/Private/PostProcessEyeAdaptation.usf::EyeAdaptationCommon` | Target/current exposure and metered-luminance outputs |
| `Shaders/Private/PostProcessHistogramCommon.ush::ComputeEyeAdaptation` | Linear-to-exponential response in log space |
| `Shaders/Private/Histogram.usf::CalculateBucketsAndWeights` | Interpolated bins and black-bucket influence |
| `Shaders/Private/PostProcessTonemap.usf` | Pre-exposure removal and global gain |

## 3. Deliberate Oxygen choices

| Area | Choice and reason |
| --- | --- |
| Calibration | Preserve the accepted key/EV equation and explicit client defaults; compare equivalent effective bias with UE |
| Ownership | Producers own persistent view identity; Renderer Core validates/routes; PostProcessService owns GPU exposure state |
| Authority | GPU gain and frame-pinned exposure binding; nonblocking CPU status controls resource formats, not the numerical gain |
| Camera cuts | Remeter by default; explicit Preserve and Seed support gameplay/cinematic intent |
| Shared views | One owner and prior-generation readers give scheduling independence, at the documented one-frame latency |
| Adaptation | Exact linear/exponential crossing integration gives consistent trajectories across frame schedules |
| Histogram | 256 bins and a bounded normalized-view grid retain current bin resolution while bounding cost and accumulation |
| Mask/curve | Scalar texture multiplied by analytic weights; at most 64 curve keys evaluated once per view |
| HDR precision | FP32 SceneColor accumulation in both modes; checked conversion into existing resolved color, FP16 when suitable and FP32 during recovery; other qualified radiance products remain dual-format; no generic precision-management service |
| FP32 performance baseline | EX051-04 adds FP32 storage with P=1 and normal exposure/history/events, omitting FP16-admission work. The existing format-only `fp32` control still executes certification and remains a numerical diagnostic. |
| Precision economics | EX051-05 uses four production/FP32-only pairs. EX051-09 selects the operating policy from those results and specifies admission/retry transitions. |
| Fallback retention | EX051-10A replaces whole-family retention with an independent SceneColor lease; unrelated attachments remain reusable while delayed color consumers retain their input. |
| Validation | Production renderer paths, independent expected values, readable LightBench scenes and native MultiView visual isolation |

UE's borrower-bootstrap exception, generic curve infrastructure, legacy exposure
modes, local exposure, mobile permutations and a second Basic metering algorithm
are not part of this global-exposure product.

## 4. Integration reminders

Use the [slice file map](../plan/exposure-and-lightbench-correction.md#file-entry-points)
for concrete implementation entry points.

- Migrate atmosphere LUT producers and sky/AP consumers together. Convert temporal
  RGB by its stored P; leave transmittance, coverage, depth and normals unscaled.
- Keep transient view events and GPU history out of packed scene records.
  Version authored fields and retain deterministic old-record defaults.
- Treat DemoShell's saved-settings reapplication as a separate activation policy
  from environment override. LightBench owns its experiment inputs.
- MultiView uses the same public view contracts and measurement service. Compare
  isolated per-view outputs before checking final composition.
- Extend existing capture/assertion tools; do not introduce another validation
  framework for the demos.
