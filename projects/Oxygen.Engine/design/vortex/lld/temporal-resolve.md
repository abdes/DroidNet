# Temporal Resolve LLD

**Phase:** post-baseline PostProcessService capability
**Deliverable:** [VX-POST-02](../milestones/VX-POST-02/README.md)
**Status:** `planned` — decisions made; starts
after camera-relative rendering (VX-VIEW-03).

## Summary

Vortex gets one temporal resolve: a temporal super-resolution (TSR) pass at
the start of Stage 22 that anti-aliases, reconstructs from a lower render
resolution when a view asks for it, and integrates the per-frame noise of
stochastic effects. The design follows UE5.7's TSR (§1.3). Classic TAA is not
built first: TSR is its successor, and building both would discard one.

- **Jitter.** The renderer jitters each stateful view's projection with a
  Halton (2,3) sequence whose length is a prime, and publishes the jitter, a
  temporal frame index and a blue-noise texture to every stochastic effect.
- **Velocity.** Opaque geometry that moved writes jitter-free velocity and a
  flag; unmoved geometry writes the flag clear, and the resolve reconstructs
  camera motion from depth. Translucent materials opt into velocity and into
  a responsive (low-history) flag.
- **History.** Each view state owns its histories. They store color scaled
  by the frame's exposure, are rebased when exposure changes, survive
  resolution changes by resampling, and reset only on discontinuities and
  format changes.
- **Passes.** Velocity dilation, history decimation with parallax
  disocclusion, shading rejection against a guide history, flicker and thin
  geometry handling, spatial anti-aliasing of rejected pixels, history update
  with per-pixel validity, and the final resolve to output resolution.
- **Output.** Scene-referred FP32 color at output resolution, so bloom,
  tonemapping and everything after Stage 22 keep their existing contract.

## 1. Scope and Context

### 1.1 What This Covers

- Jitter, temporal sampling and render-resolution scaling per view (§4).
- The velocity contract: formats, writers, flags and camera-motion
  reconstruction (§5).
- The TSR passes, their inputs, outputs and formats (§6).
- History ownership, exposure handling and invalidation (§7).
- Translucency, editor aids, stateless views and multi-view (§8–§9).
- Diagnostics, failure modes and gates (§10–§12).

Not covered: dynamic resolution (VX-VIEW-02), depth of field and motion blur
(VX-POST-03, [#47](https://github.com/abdes/DroidNet/issues/47)), separate translucency composition (VX-TRANS-01), vendor
upscalers, history resurrection, and skinned and morph velocity, which arrive
with their producers (VX-MOTION-01, [#48](https://github.com/abdes/DroidNet/issues/48)).

### 1.2 Stage Position

| Position   | Stage                                                                                 |
| ---------- | ------------------------------------------------------------------------------------- |
| Producers  | Stage 9 opaque velocity and flags; Stage 19 (its reserved owner) translucent velocity |
| Exposure   | Solved from the render-resolution FP32 accumulation before Stage 21 (unchanged)       |
| This       | Stage 22, first pass: temporal resolve of Stage 21's resolved scene color             |
| Successors | Stage 22 bloom and tonemap at output resolution; post-tonemap editor aids; Stage 23   |

A future depth-of-field pass (VX-POST-03) runs before the resolve and motion
blur after it, as in UE5.7.

### 1.3 Authority

UE5.7 (`F:\Epic Games\UE_5.7\Engine`) is the reference. `R` is
`Source/Runtime/Renderer/Private`, `S` is `Shaders/Private`.

| Topic                   | UE5.7 source                                                                                                                                                                                               |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Jitter and sample count | `R/SceneVisibility.cpp:5154-5406` (Halton, prime sequence length for TSR, mip bias)                                                                                                                        |
| Unjittered matrices     | `Source/Runtime/Engine/Public/SceneView.h:597-666`; `Engine/Private/SceneView.cpp:2770-2792` (`ClipToPrevClip`, `TemporalAAJitter`)                                                                        |
| Velocity                | `R/VelocityRendering.cpp:758-890`; `S/Common.ush:2043-2145`; `S/VelocityCommon.ush:9-40`                                                                                                                   |
| TSR passes              | `R/PostProcess/TemporalSuperResolution.cpp:1784-3468`                                                                                                                                                      |
| TSR shaders             | `S/TemporalSuperResolution/TSRDilateVelocity.usf`, `TSRDecimateHistory.usf`, `TSRRejectShading.usf`, `TSRUpdateHistory.usf`, `TSRDepthVelocityAnalysis.ush`, `TSRColorSpace.ush`, `TSRShadingAnalysis.ush` |
| History and resets      | `R/SceneRendering.h:839-1091`; `R/SceneVisibility.cpp:5424-5497`                                                                                                                                           |
| Pre-exposure            | `R/PostProcess/PostProcessEyeAdaptation.cpp:1383-1494`; `R/PostProcess/TemporalAA.cpp:818`                                                                                                                 |
| Translucent velocity    | `R/VelocityRendering.cpp:1040-1076`; `S/VelocityShader.usf:195-236`                                                                                                                                        |
| Frame order             | `R/PostProcess/PostProcessing.cpp:928-1005`                                                                                                                                                                |

### 1.4 Rejected Alternatives

| Alternative                            | Why rejected                                                                                                                                                                                            |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Classic TAA first, TSR later           | TSR replaces TAA's clamp, filter and history in full; the TAA work would be discarded.                                                                                                                  |
| Vendor upscaler only (DLSS, FSR, XeSS) | Vendor-specific or opaque about inputs; Oxygen needs control of rejection for VSM noise, hair, water and translucency. Vendor upscalers can be added later behind the same inputs (§6.1).               |
| Spatial AA only (FXAA, SMAA)           | Does not integrate stochastic effects, so VSM SMRT and future hair and volumetrics would stay noisy.                                                                                                    |
| MSAA                                   | Incompatible with the deferred GBuffer at acceptable cost and does not resolve shading noise; it stays a separate editor-primitive item (VX-AA-01, [#46](https://github.com/abdes/DroidNet/issues/46)). |

## 2. Decisions

Decided 2026-10-12; the milestone records them in the
[milestone](../milestones/VX-POST-02/README.md#decisions).

| ID     | Decision                                                                                                                                                                                                                                                                                                                                                                                | Status  |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| TR-D1  | **Algorithm.** A TSR-class resolve after UE5.7; no classic TAA phase. Vendor upscalers later through the same input record.                                                                                                                                                                                                                                                             | Decided |
| TR-D2  | **Feature scope.** Velocity dilation, parallax disocclusion, shading rejection with a guide history, flicker and moiré handling, thin-geometry detection, spatial AA, validity-based history update, history at 100–200 % of output. Deferred: resurrection, reprojection fields, async compute, lens distortion.                                                                       | Decided |
| TR-D3  | **Upscaling.** Per-view screen percentage (50–100 %) in this milestone; dynamic resolution stays in VX-VIEW-02.                                                                                                                                                                                                                                                                         | Decided |
| TR-D4  | **Velocity contract.** `RG16_FLOAT` jitter-free velocity plus an `R8_UINT` temporal-flags target. Unmoved geometry clears the written flag and gets camera motion from depth. Forward opaque writes velocity; Stage 3 writes none.                                                                                                                                                      | Decided |
| TR-D5  | **Jitter.** Renderer-owned per view state: Halton (2,3), uniform within the pixel; sequence length is the smallest prime ≥ 8 × (output/render)²; texture mip bias `log2(render/output)`.                                                                                                                                                                                                | Decided |
| TR-D6  | **Temporal sampling contract.** Every stochastic effect takes its frame index, sequences and blue noise from one renderer-published set; no effect keeps a private Halton or frame counter.                                                                                                                                                                                             | Decided |
| TR-D7  | **Placement.** First pass of Stage 22. Input is Stage 21's resolved scene color with the exposure Stage 21 recorded: pre-exposed `RGBA16_FLOAT` when the view's HDR format is half, otherwise scene-referred FP32, which the resolve multiplies by `E`. There is no second color path. Exposure metering stays at render resolution before Stage 21.                                    | Decided |
| TR-D8  | **History domain.** `RGBA16_FLOAT` color scaled by the frame's solved exposure `E`, rebased by `E_current/E_stored`; output scene-referred FP32 (`÷E`). Alpha (coverage) is never scaled.                                                                                                                                                                                               | Decided |
| TR-D9  | **History ownership.** Per `ViewStateHandle`, rolled forward only after the view's frame recorded. One temporal-history owner takes every per-view history off `ViewId` keys: volumetric fog, screen HZB, occlusion culling, the retained resolved color and depth, previous depth and velocity, ground-grid smoothing, cascade quality and shadow occlusion.                           | Decided |
| TR-D10 | **Material flags.** `temporal_responsiveness` (normal, responsive), `has_pixel_animation` and `output_translucent_velocity` become cooked material properties (cooked format, cooker, editor inspector). They feed the existing runtime path, `MaterialMotionCapabilityFlags` → `MotionPublicationCapabilityBits`, which gains the translucent-velocity bit; there is no second source. | Decided |
| TR-D11 | **Stateless views.** No jitter and no history: the resolve runs its spatial AA only. Quality captures take a view state and a warm-up frame count.                                                                                                                                                                                                                                      | Decided |
| TR-D12 | **Post-resolve depth.** The resolve publishes an output-resolution closest depth; editor aids after Stage 22 test against it, unjittered, at output resolution.                                                                                                                                                                                                                         | Decided |

## 3. Inputs and Products

| Input          | Format / source                                                                                                                                            |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scene color    | Stage 21 `ResolvedSceneColor` at render resolution, alpha = coverage: pre-exposed `RGBA16_FLOAT`, or scene-referred `RGBA32_FLOAT`; the record names which |
| Scene depth    | `SceneDepth`, reversed-Z                                                                                                                                   |
| Velocity       | `SceneVelocity` `RG16_FLOAT` (§5)                                                                                                                          |
| Temporal flags | `SceneTemporalFlags` `R8_UINT` (§5)                                                                                                                        |
| Exposure       | The frame's solved exposure state (GPU), read for `E`                                                                                                      |
| View           | Captured in `TemporalResolveInputs`: jitter, stable and reprojection matrices, frame index, extents                                                        |
| History        | §7                                                                                                                                                         |

| Product                 | Format                                              | Consumers                                               |
| ----------------------- | --------------------------------------------------- | ------------------------------------------------------- |
| Resolved color          | `RGBA32_FLOAT` at output resolution, scene-referred | Bloom, tonemap                                          |
| Output closest depth    | `R32_FLOAT` at output resolution                    | Post-resolve editor aids (TR-D12)                       |
| Previous resolved color | The previous frame's resolved color with its `E`    | Future screen-space reflections and GI (VX-INDIRECT-01) |

## 4. Jitter, Sampling and Resolution

**Render resolution.** A view's render extent is its output extent times its
screen percentage (TR-D3), rounded to whole pixels. Scene textures, Stage
3–21 and the exposure meter run at render resolution; the resolve writes
output resolution. The scene-texture lease key carries the render scale
([multi-view §9.1](multi-view-composition.md#91-scenetexturelease)),
which this design unpins from 1.0.

**Sequence.** For a stateful view, the jitter index advances once per frame
and wraps at the sequence length `N`: the smallest prime ≥ 8 ×
(output/render)². A prime length stays uncorrelated with power-of-two noise
indices (UE5.7 `SceneVisibility.cpp:5233-5251`). Sample `i` is
`(Halton(i+1, 2) − 0.5, Halton(i+1, 3) − 0.5)` render pixels. A discontinuity
restarts the index at zero.

**Projection.** `view_to_clip` adds `2·j.x/W` and `−2·j.y/H` to the
projection's clip-space offset terms; `stable_view_to_clip` stays unjittered.
Velocity, reprojection (volumetric fog included), culling histories and HZB
reprojection use stable matrices; rasterization and depth reconstruction use
the jittered ones. Matrix names are those of
[camera-relative §5.2–5.3](camera-relative-rendering.md#52-viewconstants),
which lands first.

**Mip bias.** Material texture sampling adds `log2(render/output)` plus a
fixed −0.5 when the resolve is active, so upscaled views keep output-resolution
texture detail.

**Temporal sampling contract (TR-D6).** Camera entry points, volumetric fog
among them, read these fields from `ViewConstants`, which gains them in a
schema revision; non-camera passes (the resolve, VSM SMRT) receive copies in
their inputs records ([shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity)):

| Field                                         | Meaning                                                            |
| --------------------------------------------- | ------------------------------------------------------------------ |
| float2 `pixel_jitter`                         | Current jitter in render pixels                                    |
| uint `jitter_index`, `jitter_sequence_length` | Current sample and `N`                                             |
| uint `temporal_frame_index`                   | Per view state, increments every frame, never wraps within a run   |
| uint `blue_noise_slot`                        | Blue-noise texture array the renderer creates from `BlueNoiseData` |
| float `render_to_output_scale`                | Render extent / output extent                                      |
| float `texture_mip_bias`                      | §4 mip bias                                                        |

Volumetric fog, VSM SMRT and every later stochastic effect index noise with
`temporal_frame_index` and the blue-noise texture. Stateless views publish
zero jitter and `temporal_frame_index = 0`. The existing `BlueNoiseData` (49
slices of 64×64, from a frequency-filtered generator) is used only if it
passes the spectral check in G10; otherwise it is regenerated with
void-and-cluster.

## 5. Velocity Contract

| Target               | Format       | Meaning                                                                                     |
| -------------------- | ------------ | ------------------------------------------------------------------------------------------- |
| `SceneVelocity`      | `RG16_FLOAT` | `stable_clip_now.xy − stable_clip_prev.xy` in NDC; valid only where the written flag is set |
| `SceneTemporalFlags` | `R8_UINT`    | Bit 0 velocity written; bit 1 pixel animation; bit 2 responsive; other bits zero            |

Both use stable (jitter-free) matrices, so a static scene under a static camera
has zero velocity with jitter on.

**Writers.**

| Stage                                    | Geometry                                                                                                                                                 | Writes                                                                                 |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| 3 Depth prepass                          | All                                                                                                                                                      | Nothing (the current zero-velocity output and its publication are removed)             |
| 9 Base pass, deferred and forward opaque | Draws whose transform changed (`kWorldTransformUnchanged` clear), material WPO, animated materials, motion-vector world offset, future skinned and morph | Velocity and flags with bit 0                                                          |
| 9 Base pass                              | Unchanged draws                                                                                                                                          | Flags with bit 0 clear (and bit 1 for pixel-animation materials)                       |
| 19 Translucent velocity                  | Translucent materials with `output_translucent_velocity` or `temporal_responsiveness = responsive`                                                       | Depth (velocity materials), velocity and flags where opacity exceeds the material clip |

Every lit view therefore publishes `kSceneVelocity` from Stage 9; the
base-pass velocity merge and the Stage 23 previous-velocity copy key off that
publication instead of Stage 3's.

**Camera motion.** Where bit 0 is clear, the resolve reconstructs velocity
from depth with `stable_clip_to_previous_clip`
([camera-relative §5.3](camera-relative-rendering.md#53-viewhistoryframebindings)).
An invalid view history makes the resolve drop its history; it never zeroes
object motion.

**Coverage.** Deferred and forward opaque views write velocity; the current
forward-only exemption is removed. Skinned and morph geometry joins when its
producer lands (VX-MOTION-01); until then it is rigid and covered by its
transform.

## 6. Passes

All passes are compute, one dispatch each, per view with a valid input set.

### 6.1 Input Record

`TemporalResolveInputs` is a new record in the shader contract schema
(`Vortex/Meta/ShaderContracts.yaml`). It names every input in §3 plus the
render and output extents and the history set. The resolve passes are non-camera passes: each
reads this record, captured at record time, and never binds `b1`
([shader-contracts §4.9](shader-contracts.md#49-pass-inputs-and-data-integrity)). It is the only interface between the renderer
and the resolve; a vendor upscaler would take the same record.

### 6.2 Pass List

| Pass                | Resolution | Reads                                                            | Writes                                                                                                                                                     |
| ------------------- | ---------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ClearPrevAtomics    | render     | —                                                                | `PrevAtomics` `R32_UINT`                                                                                                                                   |
| DilateVelocity      | render     | depth, velocity, flags                                           | `ClosestDepth` `R16_FLOAT`, `DilatedVelocity` `RG16_FLOAT`, `DilateMask` `R8_UNORM` (moving, depth edge); scatters the closest occluder into `PrevAtomics` |
| DecimateHistory     | render     | dilated velocity, `PrevAtomics`, previous guide, moiré, coverage | `ReprojectedGuide` `RGBA16_FLOAT`, `DecimateMask` `RG8_UNORM` (parallax rejection, hole-filled velocity)                                                   |
| DetectThinGeometry  | render     | depth, closest depth, coverage history                           | `ThinGeometry` `R8_UNORM`, new coverage history                                                                                                            |
| RejectShading       | render     | scene color, reprojected guide, masks, flags                     | `HistoryRejection` `RGBA8_UNORM`, new guide, new moiré, `InputLuma` `R8_UNORM`                                                                             |
| SpatialAntiAliasing | render     | input luma, rejection                                            | `AntiAliasing` `RG8_UINT` (edge direction and offset)                                                                                                      |
| UpdateHistory       | history    | scene color, previous color and metadata, all masks, `E`         | new history color and metadata                                                                                                                             |
| ResolveHistory      | output     | new history color, closest depth                                 | resolved color, output closest depth                                                                                                                       |

**DilateVelocity.** The closest depth (reversed-Z maximum) of the 3×3
neighborhood selects the velocity sample, as in UE5.7
`TSRDepthVelocityAnalysis.ush:75-317`. Camera motion is reconstructed where
the written flag is clear. Moving pixels scatter their closest depth into
the previous frame's position for parallax disocclusion.

**DecimateHistory.** Reprojects the previous guide, moiré and coverage
histories to render resolution and marks pixels whose previous-frame occluder
is nearer than the reprojected surface (disocclusion).

**RejectShading.** Compares the input with the reprojected guide in the
guide color space `x/(x + k)`, over a tiled 3×3 neighborhood, and produces the
per-pixel rejection that clamps history trust. Pixels flagged responsive
take a minimum rejection; pixel-animation pixels skip the guide comparison.
Flicker detection compares against the moiré history with a period scaled to
the frame rate (UE5.7 `r.TSR.ShadingRejection.Flickering.*`).

**SpatialAntiAliasing.** Finds edge direction and sub-pixel offset on input
luma for pixels the history cannot cover.

**UpdateHistory.** For each history pixel: gathers the jittered input samples
(brought to exposed space: FP32 input times `E`, half input as is) with
HDR-weighted reconstruction weights (Karis `1/(1+luma)` in exposed space), fetches the previous history with Catmull-Rom filtering, rebases it by
`E_current/E_stored`, clamps it by the rejection, and accumulates with a
validity that counts effective samples up to the history sample budget. The
metadata stores the validity.

**ResolveHistory.** Copies or downsamples the history to output resolution
(history percentage above 100 %), divides by `E` and writes the resolved
color and the output closest depth.

## 7. History

Each `ViewStateHandle` owns a history set while the resolve is enabled for it:

| History             | Format         | Resolution |
| ------------------- | -------------- | ---------- |
| Color               | `RGBA16_FLOAT` | history    |
| Metadata (validity) | `R8_UNORM`     | history    |
| Guide               | `RGBA16_FLOAT` | render     |
| Moiré               | `RGBA8_UNORM`  | render     |
| Coverage            | `R8_UNORM`     | render     |
| Stored `E`          | 4 bytes        | —          |

Each history is double-buffered: one read, one written each frame.

**Exposure (TR-D8).** Color is stored multiplied by the frame's solved
exposure `E`, so values sit in display-relative range and fit half precision;
the sun disk saturates at the half maximum as in UE5.7. Reading the history
multiplies RGB by `E_current/E_stored`; alpha is never scaled. This implements
the exposure history rule in [exposure.md](exposure.md) and replaces the
unused `post_history_srv` handoff
([post-process §post chain](post-process-service.md#post-chain-and-qualification)),
which is removed.

**Ownership (TR-D9).** A single renderer temporal-history owner keys every
temporal product by `ViewStateHandle`
([multi-view §9.3](multi-view-composition.md#93-persistent-per-view-resources)):
this resolve and every per-view history keyed by `ViewId` today: volumetric
fog, screen HZB, occlusion culling, the retained `ResolvedSceneColor`,
`ResolvedSceneDepth`, `PrevSceneDepth` and `PrevVelocity` artifacts,
ground-grid smoothing, cascade quality and shadow occlusion. Exposure state
is already keyed by its view state. Histories roll forward only when the
view's frame recorded successfully; `PreviousViewHistoryCache`,
`RigidTransformHistoryCache` and `DeformationHistoryCache` follow the same
rule.

**Invalidation.**

| Event                                                               | Effect                                                           |
| ------------------------------------------------------------------- | ---------------------------------------------------------------- |
| View discontinuity (camera cut, world replacement, device recovery) | History dropped; jitter index restarts                           |
| State handle released or replaced                                   | History released after its readers' fences                       |
| History or guide format change, resolve disabled                    | History dropped                                                  |
| Render or output resolution change, screen percentage change        | History resampled through its stored view rectangle; not dropped |
| Debug mode that bypasses the resolve                                | History not advanced                                             |
| Read-only views (picking, captures marked read-only)                | History read, not written                                        |

## 8. Translucency, Editor Aids and Stateless Views

**Translucency.** Stage 18 still composites into `SceneColor`. The Stage 19
translucent velocity pass gives materials that opt in their own velocity and
the responsive flag (§5). Without either, the resolve's shading rejection
handles translucent changes. A separate translucency input is added with
VX-TRANS-01 through the input record.

**Editor aids (TR-D12).** Wireframe, ground grid, selection outline, view
overlays and picking run after Stage 22 at output resolution with unjittered
projection. They test depth against the output closest depth, so their edges
agree with the resolved image within one render pixel.

**Stateless views (TR-D11).** A view without a state handle renders without
jitter and runs DilateVelocity, RejectShading without history,
SpatialAntiAliasing and ResolveHistory. Offscreen captures that need converged
quality take a state handle and render a stated number of warm-up frames.

## 9. Multi-View

Every stateful view has its own jitter sequence, temporal frame index and
history set. Views never share history. Editor panes are separate views with
separate histories. Memory scales with the number of stateful views and their
history resolution.

## 10. Diagnostics

Debug modes: velocity (written and reconstructed), temporal flags, history
rejection, validity, parallax disocclusion, thin geometry and the guide.
Statistics: per-view history memory and the active sequence length.

## 11. Failure Modes

| Failure                                    | Behavior                                                                            |
| ------------------------------------------ | ----------------------------------------------------------------------------------- |
| Missing required input in the input record | The view's resolve is rejected on the CPU with a logged reason; no fallback output. |
| History allocation failure                 | Logged; the view runs stateless for that frame.                                     |
| Non-finite input                           | Clamped before accumulation; counted in the diagnostics buffer.                     |

## 12. Validation Gates

| Gate | Check                                                                                                                                                                                                 |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1   | Velocity: zero for a static scene and camera with jitter on; camera motion from depth matches the analytic value; moving objects match their transforms.                                              |
| G2   | Convergence: a static view converges to a 64-sample supersampled reference within tolerance and stops changing frame to frame.                                                                        |
| G3   | Ghosting: a moving object over a high-contrast background leaves no trail beyond the tolerance after a fixed frame count; disocclusions are rejected.                                                 |
| G4   | Discontinuity: the first frame after a cut uses no history.                                                                                                                                           |
| G5   | Exposure: a 4× exposure step produces no flash or fade beyond the exposure transition itself.                                                                                                         |
| G6   | Upscaling: at 50 % screen percentage, output extent, mip bias and edge quality against the native reference meet tolerance.                                                                           |
| G7   | Multi-view: two stateful views keep independent histories; a stateless view allocates none.                                                                                                           |
| G8   | Translucency: a responsive material over moving content leaves no trail; a velocity-writing translucent object reprojects.                                                                            |
| G9   | Thin geometry and moiré: a sub-pixel wire scene and a fine periodic pattern stay stable.                                                                                                              |
| G10  | Temporal sampling: volumetric fog uses the shared frame index and blue noise; no private sequence remains. The blue noise passes a spectral check (low-frequency energy below a set bound per slice). |
| G11  | Editor aids: grid, selection outline and picking are correct at 50 % screen percentage.                                                                                                               |

## 13. Open Questions

- Editor exposure of the method, screen percentage and history percentage
  settings is planned with the editor; this design only provides them as
  per-view settings.
