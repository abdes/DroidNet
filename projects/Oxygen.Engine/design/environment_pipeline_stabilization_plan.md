# Environment Pipeline Stabilization Plan (Code-Grounded)

## Objective
Stabilize atmosphere + sky capture + IBL publication so frame output is coherent under startup, preset loads, and live edits (sun drag, twilight), while keeping warning/error diagnostics enabled.

## Scope
- In scope:
  - `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.*`
  - `src/Oxygen/Renderer/Passes/IblComputePass.cpp`
  - `Examples/DemoShell/Services/EnvironmentSettingsService.*`
  - `Examples/DemoShell/Ui/EnvironmentVm.*`
  - `Examples/DemoShell/Runtime/ForwardPipeline.cpp`
  - `src/Oxygen/Renderer/Renderer.cpp`
- Out of scope for this plan:
  - Tone mapping/banding quality work (dithering/precision changes)
  - Large service class split refactor
  - Blue-noise driven sky dithering improvements (deferred to post-stabilization phase)

## Current Failures (Verified)

### 1) Published snapshot vs getter snapshot mismatch
- `EnvironmentStaticDataManager::UploadIfNeeded()` can upload `last_coherent_snapshot_` when gate blocks.
- Public getters in `EnvironmentStaticDataManager.h` (`GetSkyLightCubemapSlot`, `IsSkyLightCapturedSceneSource`, etc.) currently read `cpu_snapshot_`.
- Result: dependent passes can consume state from `cpu_snapshot_` while shaders sample uploaded `last_coherent_snapshot_`.

### 2) Coherence gate can hold stale content indefinitely
- `RefreshCoherentSnapshotState()` sets `use_last_coherent_fallback_`.
- `UploadIfNeeded()` then publishes fallback snapshot whenever incoherent.
- If convergence does not happen quickly, frame publication stays stale and logs spam forever.

### 3) IBL content version tagging is fragile
- `IblComputePass::DoExecute()` computes `source_content_version` through env-manager getters.
- If getters observe a different snapshot than what is currently published, content-version tracking drifts.
- Result: repeated stale-IBL warnings and non-converging state.

### 4) Settings transaction noise drives unnecessary churn
- `EnvironmentSettingsService::MarkDirty()` increments revision and marks pending on each setter.
- Preset application in VM uses many setters in one batch; without strict transactional suppression this causes noisy dirty/effective masks and repeated applies.

### 5) Runtime config is called every frame by design
- `DemoShell::UpdatePanels()` calls `EnvironmentVm::SetRuntimeConfig(...)` per frame.
- This is acceptable only if idempotent and low-noise.
- Any logging or heavyweight action in this path must be edge-triggered only.

## Stabilization Principles
1. Single frame truth: passes and shaders must read the same published snapshot.
2. No silent content substitution: gate may block/flag, but cannot hide state transitions by indefinitely re-publishing old content.
3. Version monotonicity: capture generation and IBL source-content version must converge deterministically.
4. Batch semantics: preset load should produce one logical environment transaction.
5. View intent is authoritative: only `with_atmosphere=true` views run atmosphere-dependent pipeline work.

## Target Invariants
- INV-1: Getter-visible environment state equals uploaded snapshot state for current frame.
- INV-2: For captured-scene skylight, `capture_gen - ibl_source_content_version <= 1` after transient work settles.
- INV-3: No atmosphere/IBL updates for views with `with_atmosphere=false`.
- INV-4: One preset apply corresponds to one consolidated environment apply cycle.
- INV-5: No perpetual coherence-gate blocked loop without explicit error escalation and recovery path.

## Implementation Plan

## Phase 0 - Add explicit publication state (foundation)
Files:
- `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h`
- `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp`

Changes:
- Add `published_snapshot_` per view (inside `ViewState`), updated exactly when upload payload is chosen.
- Route all public getters to `published_snapshot_` (not `cpu_snapshot_`).
- Add assertion/error log if getter is called before first publication.

Acceptance:
- Getter values match the snapshot logged in `EnvStatic: uploaded ...` for the same frame/view.

## Phase 1 - Coherence gate redesign (no indefinite stale fallback)
Files:
- `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp`

Changes:
- Replace indefinite fallback publication with explicit policy:
  - Short transition window allowed.
  - If incoherence exceeds threshold, publish current snapshot with error flagging (do not keep old snapshot forever).
- Keep warnings/errors, but make gate a diagnostic/protection mechanism rather than a long-lived content override.
- Add one escalation log on threshold crossing, then rate-limited logs.

Acceptance:
- No infinite `coherence gate still blocked` loop for stable scenes.
- No frame stream permanently pinned to stale fallback content.

## Phase 2 - IBL source-content version correctness
Files:
- `src/Oxygen/Renderer/Passes/IblComputePass.cpp`
- `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.*`

Changes:
- Compute `source_content_version` from published captured-scene source for the active view.
- Eliminate dependence on possibly stale/non-published snapshot access paths.
- Log error if captured-scene path regenerates IBL with unresolved (`0`) source content version.

Acceptance:
- Under steady state, stale-IBL warnings stop and convergence occurs.

## Phase 3 - Settings apply transaction hardening
Files:
- `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`
- `Examples/DemoShell/Services/EnvironmentSettingsService.h`

Changes:
- Make `BeginUpdate/EndUpdate` truly transactional:
  - During update depth > 0, accumulate dirty masks without repeated apply triggers.
  - On outer `EndUpdate`, emit one consolidated dirty mask.
- Keep current warning/error logs for clamping and invalid transitions.
- Add no-op suppression for sun-domain updates when canonical sun state is unchanged.

Acceptance:
- Preset apply logs one consolidated apply cycle (plus expected async render pass work).
- No repeated apply loop with unchanged atmosphere hash.

## Phase 4 - View eligibility enforcement and cleanup
Files:
- `Examples/DemoShell/Runtime/ForwardPipeline.cpp`
- `src/Oxygen/Renderer/Renderer.cpp`
- `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp`

Changes:
- Ensure non-atmosphere views do not execute atmosphere LUT/capture/IBL flow.
- On `with_atmosphere` transition true -> false, call state erase and stop updates for that view immediately.
- Keep explicit warning if any atmosphere update is attempted for a non-atmosphere view.

Acceptance:
- Zero atmosphere/IBL warnings for non-atmosphere views in normal runs.

## Phase 5 - Startup preset determinism
Files:
- `Examples/DemoShell/Ui/EnvironmentVm.cpp`
- `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`
- `Examples/DemoShell/DemoShell.cpp`

Changes:
- Preserve mode contract:
  - `-2` Use Scene -> apply scene
  - `-1` Custom -> load persisted custom
  - `>=0` Built-in -> apply preset
- Ensure `SetRuntimeConfig` remains idempotent and edge-triggered by scene change for startup apply.
- Keep startup INFO logs one-shot, never per-frame.

Acceptance:
- Startup behavior follows persisted `environment_preset_index` exactly and deterministically.

## Diagnostics Policy (Keep Warnings/Errors)
Do not remove existing abnormal-state warnings/errors. Keep and refine:
- Stale IBL generation warnings/errors
- Coherence blocking warnings/errors
- Invalid source state warnings
- Add:
  - `published vs getter state mismatch` error (should never happen after Phase 0)
  - `atmosphere work on non-atmosphere view` error
  - `captured-scene IBL generation with source_content_version=0` error

## Validation Matrix
1. Startup, persisted preset index:
- `-2`: scene values active.
- `-1`: custom persisted values active.
- `>=0`: matching built-in preset active.

2. Outdoor Sunny preset load:
- One consolidated settings apply.
- Brief transient warnings at most; then convergence.

3. Sun elevation drag (slow and fast):
- No black-frame oscillation from stale fallback publication.
- Capture and IBL generations converge repeatedly.

4. Twilight + auto exposure:
- No persistent stale IBL warnings.
- No coherence gate infinite loop.

## Rollout Strategy
- Implement phase-by-phase with compile + run after each phase.
- Keep logs verbose during stabilization.
- Only reduce verbosity after invariants are proven stable.

## Post-Stabilization Quality Phase (Blue Noise)
- Add explicit sky/post-process dithering path using blue-noise (or temporal blue-noise sequence) after Phase 0-5 are stable.
- Gate with a runtime toggle for A/B validation.
- Validate no regressions in twilight auto-exposure behavior while reducing visible banding.
