# Oxygen Exposure UE5.7 Parity Plan

**Date:** 2026-04-23
**Status:** `in_progress`

## 1. Goal

Bring Oxygen Vortex desktop exposure to practical parity with modern UE5.7
manual exposure and histogram-driven auto exposure, with emphasis on:

- stable frame-to-frame adaptation
- physically coherent EV100/manual-camera behavior
- authoritative pre-exposure for HDR rendering stability
- authoring and persistence surfaces that actually round-trip

This plan is intentionally scoped to what Oxygen wants.

### 1.1 Explicit Exclusions

- No mobile exposure path.
- No stereo/shared-view-state ownership path.
- No legacy UE migration helpers or legacy luminance-range compatibility code.
- No UE `Basic` auto-exposure path as a parity target.
- No UE experimental `IgnoreMaterials` metering path in the core parity gate.
- No UE local-exposure fusion path in the core parity gate.

### 1.2 Recommended Stretch Scope

After global manual/auto parity is stable, Oxygen should add bilateral local
exposure as a quality tier. It is not required to close the core manual/auto
parity gate, but it is the next modern UE-class feature that materially
improves highlight/shadow presentation.

## 2. Evidence Base

This plan is grounded in:

- the user-provided UE5.7 exposure inspection report
- Oxygen runtime code:
  - `src/Oxygen/Scene/Environment/PostProcessVolume.h`
  - `src/Oxygen/Core/Types/PostProcess.h`
  - `src/Oxygen/Scene/Camera/CameraExposure.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/PostProcess/PostProcessService.cpp`
  - `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.*`
  - `src/Oxygen/Vortex/PostProcess/Passes/TonemapPass.*`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Tonemap.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/View/ViewColorHelpers.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/Translucency/*.hlsl`
- Oxygen authoring, persistence, and UI code:
  - `Examples/DemoShell/Services/PostProcessSettingsService.*`
  - `Examples/DemoShell/UI/PostProcessPanel.cpp`
  - `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`
  - `src/Oxygen/Data/PakFormat_world.h`
  - `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/packers.py`

This is a static code-and-doc inspection. No build, test, or runtime capture
was executed in this session.

## 3. Current Oxygen Exposure Architecture

### 3.1 What Exists Today

Oxygen already has a usable Stage 22 desktop exposure chain:

1. `SceneRenderer.cpp` resolves authored post-process settings into
   `PostProcessConfig`.
2. `ExposurePass` builds a histogram and runs a one-dispatch average/adaptation
   compute shader.
3. `TonemapPass` reads the exposure buffer and multiplies scene color by the
   resolved exposure.
4. Manual and manual-camera exposure are computed from EV100 on the CPU.

That gives Oxygen:

- manual EV mode
- manual camera mode
- histogram-based auto exposure
- average / center-weighted / spot metering
- low/high percentile trimming
- min/max EV clamps
- adaptation speed up/down

### 3.2 What The Current Design Gets Wrong

The current implementation is not the authoritative frame exposure system.

The key architectural problem is that Oxygen currently has two separate
exposure authorities:

- a pre-scene manual exposure published through `PreparedSceneFrame.exposure`
  and `ViewColorData.exposure`
- a Stage 22 auto-exposure result stored in the per-view exposure buffer

That split means auto exposure is not driving the same data that sky,
atmosphere, translucency, and debug compensation consume.

### 3.3 Evidence-Backed Gaps

1. `InitViewsModule.cpp` seeds `PreparedSceneFrame.exposure` from
   `ResolvePreparedFrameExposure(...)`, which only resolves manual EV or manual
   camera EV. Auto exposure does not feed this path.
2. `ViewColorHelpers.hlsli` exposes only one scalar `GetExposure()`, sourced
   from `ViewColorData`.
3. `Sky.hlsl`, `AerialPerspective.hlsli`, and translucent shaders use that
   `GetExposure()` value as a pre-exposure-like control, but in auto mode that
   value is still the manual baseline, not the adapted auto result.
4. `ExposurePass` keeps only four floats of state
   (`average_luminance`, `exposure`, `ev`, `count`). It does not keep a UE-like
   target/current/history contract and does not support future local-exposure
   feedback.
5. `Tonemap.hlsl` simply multiplies by the exposure buffer result. It does not
   recombine `OneOverPreExposure * GlobalExposure` the way UE does.
6. `PostProcessSettingsService::ResetAutoExposure(...)` is stubbed out, so the
   intended preset-switch/camera-cut history seeding path does not currently
   exist. `UpdateAutoExposureTarget()` is also still dead scaffolding.
7. `PostProcessVolumeEnvironmentRecord` and
   `HydratePostProcessVolume(...)` do not round-trip the full authored exposure
   surface already exposed by `PostProcessVolume` and DemoShell.

## 4. Current Gaps vs Modern UE5.7

| Area | Oxygen today | Modern UE5.7 target | Priority |
| ---- | ------------ | ------------------- | -------- |
| Exposure authority | Manual pre-scene and auto Stage 22 are split | One unified eye-adaptation + pre-exposure model | P0 |
| Manual mode | Fixed CPU path bypasses shared exposure history | Manual also writes the same exposure state buffer/history | P0 |
| Pre-exposure | Partial and manual-only | CPU-side pre-exposure derived from last exposure history and published before scene rendering | P0 |
| Auto adaptation | Simple exponential lerp | Hybrid linear-to-exponential response with force-target logic | P1 |
| History/reset | Persistent per-view GPU buffer, but no proper reset wiring, no CPU-side history cache | Per-view history cache, reset/seed on cuts and preset switches | P1 |
| Metering inputs | Histogram of raw scene color with fixed Rec.709 luminance weights and analytic spot/center weighting | Histogram path plus meter mask, compensation curve LUT, consistent EV100 authoring normalization | P1 |
| Authoring defaults | Defaults drift across scene, config, frame bindings, UI service, and pak records | One authoritative default set | P0 |
| Persistence | Scene asset format drops important authored fields | Full round-trip for the active exposure authoring surface | P0 |
| Local exposure | Not implemented | Optional bilateral local exposure follow-on | P2 |

### 4.1 Immediate Inconsistencies To Remove First

- `PostProcessVolume` defaults to `ExposureMode::kAuto`, while DemoShell
  persistence defaults to manual.
- `PostProcessVolume` default `exposure_key_` is `10.0`, while DemoShell uses
  `12.5`.
- `PostProcessVolume` and `PostProcessFrameBindings` default
  `auto_exposure_speed_down` to `1.0`, while `PostProcessConfig` and DemoShell
  defaults use `3.0`.
- DemoShell exposes reset behavior for auto exposure, but the service method is
  a no-op.

These mismatches must be fixed before any parity validation can be trusted.

## 5. Recommended Target Architecture

Oxygen should converge on this runtime model:

1. Resolve authored settings into one normalized `FinalExposureSettings`
   structure for the current view.
2. Resolve the effective mode from that structure:
   `Manual`, `ManualCamera`, or `Histogram`.
3. Read last frame's per-view exposure history on the CPU and derive the
   current frame `pre_exposure` before HDR scene passes begin.
4. Publish that pre-exposure contract to shaders via a dedicated per-view
   exposure payload, not a single overloaded `exposure` scalar.
5. Render HDR scene passes in pre-exposed space.
6. Run Stage 22 exposure update into a per-view GPU exposure buffer every
   frame, including manual modes.
7. Tonemap using `one_over_pre_exposure * global_exposure`, so pre-exposed HDR
   scene color is recombined correctly.

### 5.1 Recommended Exposure State Buffer

Adopt a future-proof layout now instead of another minimal buffer:

- `slot0.x`: current/smoothed exposure scale
- `slot0.y`: target exposure scale
- `slot0.z`: average scene luminance
- `slot0.w`: exposure compensation curve result or middle-grey compensation
- `slot1.x`: average local exposure, default `1.0` until local exposure lands
- `slot1.yzw`: reserved

This mirrors the shape of the UE design closely enough to support later local
exposure without another contract break.

### 5.2 Recommended Per-View CPU Cache

Introduce explicit per-view CPU history:

- `last_exposure`
- `last_target_exposure`
- `last_average_scene_luminance`
- `last_average_local_exposure`
- `pending_force_target`
- `history_valid`

That cache can live with Stage 22 ownership, but it must be available before
scene rendering so pre-exposure is authoritative.

## 6. Executable Improvement Plan

### Phase 0 - Normalize Authority, Defaults, and Status

**Why first:** current defaults and reset behavior are internally
inconsistent, so later validation would be noisy and misleading.

**Files**

- `src/Oxygen/Scene/Environment/PostProcessVolume.h`
- `src/Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h`
- `src/Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h`
- `Examples/DemoShell/Services/PostProcessSettingsService.*`
- `design/renderer-core/physical-lighting-roadmap.md`

**Tasks**

- Pick one authoritative default set for manual/auto exposure.
- Make scene defaults, runtime defaults, frame-binding defaults, and UI
  defaults identical.
- Wire `ResetAutoExposure(initial_ev)` so preset changes and scene switches can
  seed exposure history.
- Decide whether `exposure_key` stays:
  - preferred: deprecate it from the parity authoring surface and keep only
    compensation plus target luminance
  - fallback: serialize it consistently and treat it as a temporary migration
    knob

**Exit gate**

- One documented default set exists.
- Reset/seed is no longer a no-op.
- No doc still claims auto-exposure parity is already complete.

### Phase 1 - Make Exposure State Authoritative Before Scene Rendering

**Why:** this is the largest parity gap and the main source of instability.

**Files**

- `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
- `src/Oxygen/Vortex/PreparedSceneFrame.h`
- `src/Oxygen/Vortex/Renderer.cpp`
- `src/Oxygen/Vortex/Types/ViewColorData.h`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/View/ViewColorData.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/View/ViewColorHelpers.hlsli`

**Tasks**

- Replace `PreparedSceneFrame.exposure` with a real exposure payload:
  `pre_exposure`, `one_over_pre_exposure`, and optionally `global_exposure`.
- Compute current-frame `pre_exposure` from last-frame exposure history before
  scene rendering starts.
- Stop resolving auto-mode pre-scene exposure from the manual EV fallback.
- Publish the new per-view exposure payload to all HDR-writing shader families.

**Exit gate**

- In auto mode, sky, atmosphere, and translucency no longer consume a manual
  fallback exposure scalar.
- The per-view payload explicitly distinguishes pre-exposure from final global
  exposure.

### Phase 2 - Unify Manual and Auto Through One Exposure Buffer

**Why:** UE keeps manual mode on the same exposure-history path so mode switches
and downstream consumption stay coherent.

**Files**

- `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.*`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl`
- `src/Oxygen/Vortex/PostProcess/PostProcessService.cpp`

**Tasks**

- Expand the exposure state buffer to the recommended two-`float4` contract.
- Make manual and manual-camera modes update the same exposure buffer instead
  of bypassing it.
- Add explicit `force_target` handling for:
  - first valid frame
  - camera cuts / preset resets
  - manual modes
  - invalid ranges (`min >= max`)
  - invalid speeds

**Exit gate**

- The exposure buffer is valid and meaningful in all three modes.
- Switching between manual and auto no longer swaps between unrelated runtime
  paths.

### Phase 3 - Upgrade Histogram Metering to Modern Parity

**Why:** Oxygen already has a histogram solver; this phase makes it a modern
authoring and quality match instead of a minimal compute pass.

**Files**

- `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.*`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl`
- `src/Oxygen/Scene/Environment/PostProcessVolume.h`
- `Examples/DemoShell/Services/PostProcessSettingsService.*`
- `Examples/DemoShell/UI/PostProcessPanel.cpp`

**Tasks**

- Keep histogram metering as the only non-manual parity path.
- Add meter-mask texture support.
- Add exposure-compensation curve authoring.
- Upload the compensation curve as a small LUT for the shader path.
- Evaluate the curve against average scene EV100, not against the already
  lagged adapted value.
- Decide histogram bin count:
  - preferred parity path: move to 64 bins
  - acceptable alternative: keep 256 bins only if a validation pass proves
    equivalence and debug comparability
- Make luminance weighting explicit and color-space aware.

**Exit gate**

- Histogram auto exposure supports mask, percentiles, EV clamps, curve, and
  metering modes on one consistent path.

### Phase 4 - Upgrade Temporal Adaptation and History Control

**Why:** the current simple exponential interpolation is serviceable, but it is
not the stability model UE uses for modern desktop exposure.

**Files**

- `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.*`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl`
- per-view history owner introduced in Phases 1-2

**Tasks**

- Replace the current one-parameter exponential interpolation with a
  linear-to-exponential hybrid transition in EV space.
- Keep independent speed-up and speed-down controls in EV per second.
- Cache last exposure and last average luminance on the CPU for deterministic
  pre-exposure and debug output.
- Retire stale per-view histories that are no longer referenced.

**Exit gate**

- Dark-to-bright and bright-to-dark transitions remain stable without sluggish
  long tails.
- Preset switches and camera cuts no longer flash from an unrelated exposure
  history.

### Phase 5 - Fix Authoring, Serialization, and Scene Round-Trip

**Why:** parity is not real if authored settings do not survive save/load and
  scene hydration.

**Files**

- `src/Oxygen/Data/PakFormat_world.h`
- `src/Oxygen/Data/PakFormatSerioLoaders.h`
- `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/packers.py`
- `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`
- `Examples/DemoShell/Services/PostProcessSettingsService.*`
- `src/Oxygen/Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.cpp`

**Tasks**

- Extend the post-process scene record to serialize the active exposure surface.
- At minimum round-trip:
  - manual EV or manual-camera choice
  - compensation
  - min/max EV
  - speed up/down
  - low/high percentile
  - histogram bounds
  - target luminance
  - metering mode
  - spot radius
- If `exposure_key` survives Phase 0, serialize it too.
- Add scene-loader hydration for every stored field.
- Add dumper coverage so cooked assets can be audited.

**Exit gate**

- DemoShell, scripting, scene assets, and cooked pak records all round-trip the
  same exposure surface.

### Phase 6 - Optional Quality Tier: Bilateral Local Exposure

**Why:** this is the first modern follow-on feature that materially improves
highlight/shadow presentation beyond global exposure alone.

**Files**

- new Stage 22 local-exposure pass and shader family
- `TonemapPass.*`
- per-view exposure history/cache

**Tasks**

- Implement bilateral local exposure only.
- Feed average local exposure back into pre-exposure history.
- Apply local exposure during tonemap recomposition.
- Exclude fusion/experimental modes unless Oxygen explicitly opts in later.

**Exit gate**

- Highlight and shadow retention improve measurably without destabilizing the
  global exposure solve.

## 7. Validation Plan

### 7.1 Required Automated Tests

- CPU math tests:
  - EV100 conversion
  - exposure scale from EV100
  - min/max EV clamp behavior
  - hybrid adaptation step behavior
  - force-target/reset behavior
- Exposure shader tests:
  - histogram accumulation
  - percentile solve
  - metering mode weights
  - compensation curve LUT lookup
- Serialization tests:
  - scene asset round-trip for the full exposure surface

### 7.2 Required Runtime Validation

- dark interior -> bright exterior transition
- bright exterior -> dark interior transition
- camera cut / preset switch with reset seeding
- sky-dominant scene
- emissive-dominant scene
- center-weighted and spot-meter scenarios
- manual EV -> auto -> manual-camera mode switch continuity

### 7.3 Required Capture Proof

- RenderDoc or equivalent capture proving:
  - Stage 22 histogram input texture
  - exposure buffer contents before and after update
  - pre-exposed scene color before tonemap
  - final tonemap recombination path

## 8. Recommended Implementation Order

Do the work in this order:

1. Phase 0
2. Phase 1
3. Phase 2
4. Phase 4
5. Phase 3
6. Phase 5
7. Phase 6 only if Oxygen wants the quality tier

Phase 4 comes before Phase 3 in execution because authoritative history and
reset control are prerequisites for trustworthy validation of higher-level
metering features.

## 9. Closure Criteria

Do not mark exposure parity complete until all of the following are true:

- manual, manual-camera, and histogram auto exposure run through one coherent
  exposure-state model
- pre-exposure is authoritative before scene rendering
- auto exposure no longer depends on a manual fallback for sky/translucency
- authoring settings round-trip through DemoShell, scene assets, and cooked pak
  data
- reset-on-cut / reset-on-preset-switch is implemented and validated
- validation evidence includes both automated tests and runtime capture proof
