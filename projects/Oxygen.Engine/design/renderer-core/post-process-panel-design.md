# Post-process panel design

Updated: 2026-09-16. Exposure delivery contract; runtime and visual qualification
are tracked in the [execution plan](../vortex/plan/exposure-and-lightbench-correction.md).

## Ownership and defaults

Keep the existing Service -> ViewModel -> ImGui View structure. The settings
service owns persistence and staging; camera lifecycle owns physical camera
inputs. Vortex PostProcessService owns validation, GPU gain and temporal state.
The panel never estimates automatic exposure or edits a borrowed source history.

Scene creation uses Auto, key 10, speeds 3/1 EV/s, target 0.18 and bounds [-6,16].
DemoShell's existing Manual EV9.7 preference is a separately named client default.
The physical camera defaults f/11, shutter rate 125/s, ISO100 calculate
EV13.8846475; they do not explain Manual EV9.7. Calibration key and linear gain
must not be labeled EV. Mathematical authority is the [PBR specification](physically-based-rendering.md).

## Controls and progressive disclosure

| Group | Primary controls | Advanced / read-only information |
| --- | --- | --- |
| Exposure | Enabled; Manual EV / Manual Camera / Auto; compensation EV | Requested settings revision, active revision and effective linear gain |
| Manual EV | EV100 | Key; supported-domain validation feedback |
| Manual Camera | Aperture f-number, shutter rate 1/s, ISO; computed EV100 | Camera-owned persistence path |
| Auto | Min/max EV, SpeedUp/SpeedDown EV/s, target, metering profile; Reset/Seed | Percentiles, histogram window, spot radius, mask, curve keys, black influence, D=1.5 stops |
| Tone/output | Tone curve, DisplayGamma | None still applies exposure, grading and output conversion |
| Bloom/grading | Existing intensity/threshold, saturation/contrast/vignette | Scene-referred threshold, existing production ordering |
| Diagnostics | Meter validity and effective gain | Raw meter EV/luminance, fallback/range reason, FP32 retention and pending resource status |

Compensation remains active in all exposure modes. Do not disable Manual
compensation or replace engine validation with a restrictive UI slider. Retain
saved speed numbers but label their corrected maximum EV/s meaning. Zero target
is legal and visibly black; show latent gain only in diagnostics. A synthetic
dark solve reports below-window luminance, not fabricated measured brightness.

Advanced curve controls edit the bounded piecewise-linear exposure curve, not a
general curve editor. Mask loading/failure displays requested versus effective
settings; an unavailable authored mask does not silently become average metering.
Atomically stage complete revisions through the canonical engine validator.

Reset Auto submits the public typed Remeter event for the active owned view.
Seed submits SeedFromEv100 with a new request generation. Expose rejected
ownership/mode requests and pending application. Neither operation writes CPU
history or derives gain from a diagnostic readback. Temporary wireframe/debug
unit exposure is visibly flagged and preserves authored/history state.

## Configuration isolation

Ordinary demos retain existing post_process and camera_rig persistence behavior.
Experiment-owned activation is explicit and skips saved environment,
post-process and camera reapplication together. `force_environment_override`
alone is insufficient. Window/panel preferences remain independent. LightBench
Reset restores the entire recipe and temporal state; saved modified experiments
load only through an explicit action. Batch never writes personal settings.

## Rendering and verification

The panel stages canonical settings into the scene/per-view interface. InitViews
routes resolved settings and frame exposure; SceneRenderer Stage 22 executes
post processing into the supplied output; Renderer Core composes view outputs
and display-space UI. No legacy ForwardPipeline/interceptor configuration is
part of the active Vortex integration.

Native validation covers control changes, invalid revision retention, async mask
residency, effective/requested distinction, reset/seed, source ownership and
experiment activation. Inspect 1080p, 1440p and resized layouts without repeated
or obstructive readouts. The [LightBench specification](lightbench.md) owns
experiment controls/verdicts and native visual evidence.
