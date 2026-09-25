# EX07D — Measurement baseline

Status: `validated`

| Field     | Summary                                                              |
| --------- | -------------------------------------------------------------------- |
| Outcome   | Frozen benchmark/application baselines and their measurement limits. |
| Remaining | None in the recorded scope.                                          |
| Evidence  | [Validation record](validation.md)                                   |

[Roadmap](../../../../PLAN.md) · [Design index](../../../../lld/README.md)

## Baseline qualification

**Closed (2026-09-24).** Implementation/repairs: `137b681b2`; durable baseline
register and evidence: `894a25e57`. D's scene, baseline and reporting obligations
are complete. E/F own candidate comparisons and final acceptance; the recorded
confidence limits remain binding. E is active; its work ledger and resume checkpoint are above.

**Application follow-up:** New Sponza window-close lifetime repair is verified;
the long-range point/spot receiver-footprint candidate passes 17 native tests
per configuration. Current-content performance is 16.07 FPS without
Tracy in the registered New Sponza run. The [regression analysis and UE5.7 comparison](../EX07C/sponza-analysis.md)
records the 10 m-to-4,096 m content change, measured shading costs, retained
import policy and further E qualification. The earlier many-light measurements
do not close these application gates.

The accepted measurements below count toward D within their recorded scope.
They remain controls for E/F; assigning a stage does not reset their acceptance.

| Credited work                                 | Validation                                                      |
| --------------------------------------------- | --------------------------------------------------------------- |
| Model-2 MultiView operating point             | [Evidence](validation.md#baseline-qualification--task-evidence) |
| Conventional-shadow baseline and result       | [Evidence](validation.md#baseline-qualification--task-evidence) |
| Reference, recipe and measurement foundations | [Evidence](validation.md#baseline-qualification--task-evidence) |

**Closed D baseline register:** the shared native scene now has 54 timed,
image-qualified rows across 27 presets and both families. The current 1,024-light
1080p primary is **10.043 ms deferred / 7.518 ms forward**, measured in non-Tracy
Ninja Release with native stage timing. The earlier **59.530 / 8.311 ms** fully
traced reference and its 16-row count set remain historical evidence, with the
interrupted original 4,096-light row excluded. Current Instancing and New Sponza
each have separate Tracy and non-Tracy Release application captures. The
[register](validation.md) records percentiles, CPU/GPU costs,
memory scope, images, identities, recipes and replay inputs. Preflight covers
24 benchmark rows and all application runs; the other 30 timings and noisy
B05-D retain explicit comparison limits. E performance and F acceptance remain
open; a baseline is not a performance-budget approval.

The accepted application measurements retain their original workload coverage.
The 64x36 preview and 4K/1,024-light allocation checks provide prerequisites for
the new scene, without establishing its full-resolution throughput. Analyze
existing captures for additional statistics where possible. Repeat an accepted
case only for a specific invalidating change or diagnosed measurement noise.
E compares optimizations on this scene; F reuses unaffected evidence when checking
the integrated result. No stage transition creates a fresh baseline requirement.

### EX07D delivery: many-light scene and baseline

**EX07D closed (2026-09-24):** implementation is committed in `137b681b2` and
the expanded baseline register/evidence in `894a25e57`:
54 timed, image-qualified benchmark rows and four current Instancing/New Sponza
application runs, with separate Tracy/non-Tracy Ninja Release identities,
percentiles, memory scope, images, traces, recipes and historical controls.
CPU/GPU preflight is recorded for 24 benchmark rows and all application runs;
the other 30 timings predate that requirement and are explicitly supporting
references. Noisy B05-D requires a targeted comparison before a timing claim.
See the [baseline register](validation.md). Do not restart
accepted baseline campaigns or treat slow deferred results as an architecture
policy change. Deferred draw culling must conservatively retain off-screen
lights whose influence reaches the view, as UE5.7's light-volume frustum test
does, and must not prune light/shadow or off-screen caster selection.

Credit the accepted model-2 MultiView operating point, conventional-shadow
New Sponza/Instancing measurements and B's reference/instrument qualification.
Their evidence and original coverage are recorded in
[tracker section 3.4](README.md#baseline-qualification).
Keep those results as controls for their measured workloads. Reuse existing
captures for additional statistics where possible. Reopen an accepted case only
when a specific change invalidates its evidence or diagnosed noise prevents the
required comparison; record that reason before repeating it.

D's completed delivery comprises the many-light test scene and its baseline:

**Rendering-family scope:** preserve Vortex's deferred-first desktop contract.
D measures both deferred and forward rendering on the same many-light recipes.
Deferred scaling problems remain visible baseline results and EX07E inputs;
poor performance does not remove a workload from qualification.

1. **Create a runnable, inspectable scene from the existing recipe.** Reuse
   `Test/Support/LightingWorkload.{h,cpp}` and
   `Test/Lighting/LightingWorkloads.json` under `src/Oxygen/Vortex`, along with
   the native preview's scene setup. Supply the receiver geometry, materials,
   cameras and real scene-owned lights through the production rendering paths.
   The primary preset is 1,024 lights (512 point / 512 spot) at 1920x1080, with
   at least 256 visible contributors throughout the deterministic motion cycle.
   Provide the count, distribution, motion, resolution, view and shadow variants
   in the workload envelope below as presets of the same scene. Preserve the
   frozen inputs and add the receiver/caster geometry required by each preset.
2. **Qualify that scene for measurement.** Reuse the existing references and
   focused correctness checks to verify contributions, actual output size,
   spatial lists, shadow identity and applicable mutation/lifetime behavior.
   Make the rendered scene available for visual inspection. The 64x36 preview
   and allocation tests remain useful prerequisites; full-resolution rendering
   is part of this delivery. No new oracle or general validation framework is
   required.
3. **Record its baseline.** Use the existing opt-in lighting benchmark target
   and native profiling/capture tools. Measure the primary scene in both forward
   and deferred rendering, then the prescribed count sweep and selected stress,
   motion, 4K, multi-view and shadow presets. Follow the bounded run discipline
   below; variants do not form an exhaustive Cartesian product. Record CPU/GPU
   costs, frame percentiles, actual memory, image quality and measurement noise,
   with exact launch commands, recipe/code/shader identities and capture paths.

D exits with the runnable scene/presets and their reproducible, correctly rendered
baseline report. The user decides whether its measured operating point is
acceptable. E uses this same scene and baseline for justified optimizations;
F reuses unaffected evidence and validates the integrated result. Stage changes
alone do not require another baseline campaign. The workload envelope and full
EX07 exit requirements remain unchanged.

## Supporting records

- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
