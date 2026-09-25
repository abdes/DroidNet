# EX07 — Physical lighting and scalability

Status: `validated`

| Field     | Summary                                                                            |
| --------- | ---------------------------------------------------------------------------------- |
| Outcome   | Physical lighting, resource scaling, shadow integration and measured optimization. |
| Remaining | None in the recorded scope.                                                        |
| Evidence  | [Validation record](validation.md)                                                 |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Outcome:** physically normalized directional/point/spot illumination using
[production model 2](../../../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2),
with qualified quality/performance tradeoffs and scalable supported light sets.
**Dependency:** closed EX06. **Tracked by:** EX07-01–14/GATE.

EX07 owns correctness and performance of the complete agreed lighting path,
including pre-existing defects and necessary dependency repairs. Review the path,
correct discrepancies, optimize measured costs and validate the final result.
An existing implementation limitation is repair work, not a completion exemption.

The [EX07 correctness/scalability plan](README.md)
owns workload definitions, cost attribution, capacity/overflow behavior and
performance acceptance. Execute six steps: **A contracts -> B references and
instruments -> C correctness repair -> D many-light scene and baseline -> E scalable
culling and optimization -> F final validation**. Its directional cases retain the explicit dual-source
contract in section 6. These checkpoints all belong to EX07 before EX08 begins.

**EX07D is closed (2026-09-24)** with implementation in `137b681b2` and durable
baseline evidence in `894a25e57`. The
[EX07E handoff](EX07E/validation.md#ex07e-handoff--closed)
is closed with accepted measurements and visual approval. The
[F acceptance report](EX07F/validation.md) credits that unchanged evidence
and closes overall EX07, including engine qualification, documentation and
final editor acceptance.

D credits the accepted MultiView and conventional-shadow baselines and B's
reference/instrument qualification. Its new delivery is the runnable many-light
test scene, built from the existing workload generator/manifest, and that scene's
full-resolution baseline in both deferred and forward rendering. Deferred
many-light scalability remains an explicit qualification and optimization
obligation. E/F reuse the scene and unaffected evidence; accepted
cases are repeated only for a specific invalidating change or diagnosed noise.
The [D delivery scope](EX07D/README.md#ex07d-delivery-many-light-scene-and-baseline)
and [baseline register](EX07D/validation.md) record the benchmark and
Instancing/New Sponza operating points, precise collection modes, confidence
limits and accepted historical evidence required for E/F. The register includes
54 timed benchmark rows and four application runs; preflight coverage gaps and
noisy measurements remain explicit, not silently accepted as controlled timing.

- Implement section 6's shared point/spot conversion, distance/range falloff,
  cone normalization, hard-cone limit and invalid-cone rejection. Directional
  lux and receiver cosine must pass the same complete unit-chain review.
- Cover active forward and deferred consumers; freeze working color space and
  the actual packed material/production BRDF interpretation in the independent
  oracle. Include white-source reference values and separate tint behavior.
- Use existing native scene fixtures and readbacks for a directional card,
  point distances and spot angles. Freeze the numeric case inputs and tolerances
  before accepting captures. The production helper cannot serve as its own oracle.
- Review affected authored examples/fixtures when corrected intensity changes
  their appearance. Record intentional physical recalibration; never compensate
  with a hidden exposure offset or preserve old behavior through a compatibility path.

**Calibration gate (EX07C):** both families pass photometric normalization,
material decoding, ABI and shared-model consistency checks. Report independent
BRDF/finite-source approximation differences with the current model-2 quality
measurements. Independent spot-profile integration recovers authored flux; point
ratios include known range fade.
Zero/near separation, at/beyond range, inner/outer and equal-angle cones have
explicit finite/zero/invalid expectations. Inspect the lit calibration fixtures
and run affected light/shader tests. EX08 consumes these qualified references.

**Full EX07 gate:** also pass the many-light culling/reference-image, overflow,
shader/shadow association, multi-view, resource-lifetime and editor-input cases.
Measure native quality/time/memory operating points; deliver accepted improvements
and explicit supported limits. Primary workload: 1,024 mixed local lights at
1080p; 4,096 lights, dense overlap and 4K qualify scaling. Shadowed subsets are
measured separately. Benchmarks remain opt-in executables under `Benchmarks`,
separate from correctness tests. Use existing fixtures/profiling; EX07 does not
depend on future demo or automation work. EX08 runtime instruments are removed
from package scope; EX08.2 now follows EX09 under the latest user goal.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 7 — Physical and scalable lighting | validated | EX07A–F closed (2026-09-25), including final editor approval; EX08–EX10 are also closed. | [A checkpoint](../../../lld/lighting-decisions.md), [EX07 items](#stages-and-ownership), [workloads and gates](README.md) |

## Stages and ownership

**EX07 overall: validated and closed (2026-09-25). All A–F stages and numbered deliverables are complete.**

**Production model 2 is implemented and test-validated; the measured MultiView quality/performance result was accepted.** It uses analytic finite sources,
center-cone attenuation, view-dependent energy compensation and one compact
hardware-filtered texture. Ordinary spots use cone proxies and projected shadows.
The current [PBR specification](../../../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2)
and [GPU ABI](../../../lld/lighting-gpu-abi.md) describe the implementation. Native/image
tests, numerical differences, memory and controlled 1440p measurements are below.
The broader workload, engine integration and editor acceptance are complete. The
conventional-shadow implementation sequence below was delivered before C integration. Reuse its code, unit tests, Tracy results and manual visual confirmation
in later C/D/E work. All five steps are implemented and tested. The user visually approved the
conventional-shadow changes on 2026-09-24; this prerequisite is complete.

This milestone owns EX07 deliverables. **A–F are the ordered
execution stages. EX07-01–14 are stable deliverable IDs that can span several
stages.** Completing B closes the reference/instrument portions of those IDs;
production repair, performance work and final qualification have later owners.

#### Execution stages — where we are

| Stage                                 | Result                                                                                                            | Current status                                                                                                                                                                                                                                                                                                              | Deliverable ownership                                                                                  |
| ------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| **A — Contracts**                     | Reviewed model, property inventory, canonical CPU/HLSL interface, capacities and failure contracts.               | **Complete** — [audit](EX07A/validation.md)                                                                                                                                                                                                                                                                                 | Contract/interface groundwork, especially 04 and 12; grid, lifetime and shadow contracts for 08/10/11. |
| **B — References and instruments**    | Independent physical/material/image references, native probes, frozen workloads and bounded instruments.          | **Complete** — [audit](EX07B/README.md)                                                                                                                                                                                                                                                                                     | Reference/oracle portions of 01–06; workload portion of 07; instrument portion of 13.                  |
| **C — Correctness repair**            | Validated light ingress/transport, explicit atmosphere roles, receiver/contact shadows and isolated view failure. | **Closed (2026-09-25)** — omitted caller targets built/qualified, importer lifecycle repaired, corrected Sponza content verified; [759 fresh checks and credited evidence](EX07C/validation.md)                                                                                                                             | Correctness portions of 01–04, 06, 08, 10–12.                                                          |
| **D — Many-light scene and baseline** | Reproducible benchmark and application-scene records for E/F.                                                     | **Closed (2026-09-24)** — 54 timed/image-qualified benchmark rows, four current application runs, versioned traces/settings/images and historical controls. CPU preflight coverage is 24/54 benchmark rows plus all application runs; remaining limits and noisy B05-D are explicit in the [register](EX07D/validation.md). | 07; many-light baseline/shadow-cost portions of 11/13.                                                 |
| **E — Scalable optimization**         | Fix measured deferred submission/resource costs while preserving lighting and shadows.                            | **Closed (2026-09-25)** — S1–S9 complete, 22 final benchmark rows and four final scene captures, lifetime/memory qualification and manual visual approval; [results and commit sequence](EX07E/validation.md).                                                                                                              | Optimization portions of 08–11; culling/performance diagnostics in 13.                                 |
| **F — Final delivery**                | Final-code numerical, native/editor, visual and performance gates; operating documentation and supported limits.  | **Closed (2026-09-25)** — engine qualification, documentation and editor acceptance complete; [report](EX07F/validation.md).                                                                                                                                                                                                | 14 and EX07-GATE, rechecking the final implementation of all IDs.                                      |

## Deliverable coverage

`Complete` means the entire numbered deliverable is closed. `Partial` identifies
finished foundations with explicitly named later work. `Not started` means no
qualified delivery is claimed. These are item states, distinct from stage states.

| ID / deliverable                             | Item state | Completion evidence / remaining owner                                                                                                |
| -------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| **01 — Directional lighting**                | Complete   | C transport/photometry and E native/image proof; final editor live-edit/viewport acceptance confirmed by manual checks.              |
| **02 — Point lighting**                      | Complete   | Physical/finite-source model, atomic ingress, grid scaling, cube PCF and accepted E images/timing.                                   |
| **03 — Spot lighting**                       | Complete   | Cone photometry, projected/cube coverage, property transport and both-family scaling/image qualification.                            |
| **04 — Shared interface and BRDF semantics** | Complete   | Canonical ABI/oracle, model-2 quality, failure/recovery and final shared-view native qualification.                                  |
| **05 — Material/color oracle**               | Complete   | B independent packed-material/color reference; C content transport and E native consumers.                                           |
| **06 — References and calibration**          | Complete   | A/B references credited; C repairs and E candidates qualified against applicable numerical/image oracles.                            |
| **07 — Workloads and baselines**             | Complete   | D register plus accepted E comparisons, distributions, images, memory and explicit noise/operating limits.                           |
| **08 — Complete lists and spatial culling**  | Complete   | Cooperative grid and conservative full-list fallback; boundary/dense/moving/4K/multi-view qualification.                             |
| **09 — Shader/draw performance**             | Complete   | Accepted matrix/grid/shader/submission improvements; approved PCF cost and small-workload uncertainty retained.                      |
| **10 — Resources, uploads and lifetime**     | Complete   | Actual-completion retirement, managed ownership, cross-view sharing, release/budget/failure tests and measured retention.            |
| **11 — Shadows**                             | Complete   | Off-screen caster coverage, cube PCF, all consumers and sharing qualified; final user visual and RenderScene interaction acceptance. |
| **12 — Retained properties and content**     | Complete   | C's fresh and credited transport checks; user confirmed editor creation, live edit, undo/redo and save/reopen persistence.           |
| **13 — Measurement and diagnostics**         | Complete   | Accepted E native/Tracy comparisons, ownership counters and durable results; collection modes and noise kept separate.               |
| **14 — Final validation/docs**               | Complete   | F report, reuse audit, build repairs and final editor acceptance complete.                                                           |
| **EX07-GATE — Whole slice**                  | Complete   | All A–F gates satisfied; user editor approval recorded on 2026-09-25. No captures repeated.                                          |

Status: **validated and closed (2026-09-25)**. All EX07A–F stages and
EX07-01–14 deliverables are complete, including final editor acceptance.
See the [F acceptance report](EX07F/validation.md) and
[tracker section 3.4](#stages-and-ownership).
EX07 owns end-to-end lighting correctness and performance. The
[current PBR model](../../../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2),
[GPU ABI](../../../lld/lighting-gpu-abi.md) and [property inventory](../../../lld/lighting-properties.md)
define production behavior. The [exposure delivery plan](#delivery-scope)
owns package order; this document owns workloads and qualification. A/B audits
retain the contract and independent-reference evidence.

## Outcome and boundaries

Directional, point and spot lights must produce independently predicted direct
illumination, and large supported light sets must render without missing lights,
unsafe resource use or uncontrolled CPU/GPU work. Deliver measured improvements
to the actual production bottlenecks and publish the supported operating envelope.

Preserve the [physical contract](../../../../renderer-core/physically-based-rendering.md#physical-light-conversion),
material mapping and exposure precision, scene-owned lights and explicit dual atmospheric
assignments. Qualify ordinary unassigned directionals, each atmospheric slot and
both sources together. Demo sun inference/injection remains explicitly requested.
Coordinate the directional-array implementation with its
[existing owner](../../../lld/editor-rendering.md#3-independent-directional-array-and-atmosphere-assignments);
keep one representation, no compatibility path or primary-only substitute.

Include scene eligibility/mutation, per-view culling, publication/uploads, active
forward/deferred lighting shaders, local shadow integration, submission cost,
resource lifetime, diagnostics and editor-authored input. Preserve existing
contracts for opaque/masked/forward/translucent behavior and repair violations.
Broad GI/IBL, new light types, scattering algorithms, new shadow techniques and automatic quality
degradation are outside this step. Existing shadows must remain correctly
associated and functional; a missing active-path shadow consumer is an in-scope
integration defect, not a reason to omit that case.

## Delivery responsibility

EX07 implementation owns **reviewing, repairing, improving, optimizing and
validating** the complete lighting path covered by this plan. That responsibility
includes pre-existing defects and defects discovered during execution, regardless
of which module or earlier milestone introduced them.

Production model 2 uses analytic source evaluation, center-cone attenuation,
view-dependent energy compensation and one compact hardware-filtered LUT.
Ordinary spots use cone proxies and projected shadows. Review quality, GPU/CPU
time and memory together; numerical-reference error alone does not select the
production algorithm. The PBR owner explains each compromise and rejected
alternative. Reuse material/view terms and preserve safe frames in flight while
profiling the complete application with native tools.

- Trace scene/editor input through selection, publication, culling, shaders,
  shadows, HDR accumulation and output. Review algorithms, physical units, ABI,
  validation, resource lifetime and failure behavior; the starting observations
  below are not an exhaustive defect list.
- Correct faulty production code and necessary cross-module dependencies as part
  of EX07. Coordinate architectural ownership and update its documents without
  moving a required repair to a later slice or treating it as an external blocker.
- Establish independent expected results, add meaningful regression coverage,
  and fix discrepancies before using the path as an optimization baseline.
- Profile the corrected path, implement justified improvements, then validate
  correctness and performance together on the final code. Prior test passes or
  inherited behavior do not exempt a demonstrated defect from correction.
- Keep both gates open until they pass. A defect inventory, benchmark report,
  warning, documented limitation or faster incorrect image cannot close EX07.
  Do not narrow cases, accept missing contributions or redefine supported behavior
  merely to accommodate an existing defect.

The exclusions above concern additional product families. They do not exclude
repairs needed to deliver the agreed lighting behavior. Routine review, repair,
optimization and validation are the implementation responsibility; changing the
agreed product scope or quality target is a separate decision.

## Six ordered implementation steps

| Step                                      | Required result                                                                                                                                     | Gate before proceeding                                                                                                                      |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| EX07A — Review and freeze contracts       | Canonical LLD/ABI, field-to-consumer inventory, directional authority, GPU scheduling, capacities and failure/recovery behavior.                    | One documented contract; no conflicting historical interface; complete review decisions and test obligations before consumer changes.       |
| EX07B — References and instruments        | Independent physical oracle, known-input GPU probes, unculled image reference, deterministic fixtures and bounded CPU/GPU/resource instrumentation. | Reference/instrument validity is established independently of the renderer being tested.                                                    |
| EX07C — Repair correctness                | Physical response, every retained property, directional/local/shadow identities, complete lists, ingress/round-trip behavior and safe lifetime.     | Each workload admitted to timing passes its applicable numerical/image/mutation/capacity checks.                                            |
| EX07D — Many-light scene and baseline     | Runnable deterministic many-light scene and its correctness-qualified CPU/GPU, memory and image baseline; existing accepted baselines are credited. | The scene and required presets are reproducible, their baseline evidence is recorded, and the user can assess the measured operating point. |
| EX07E — Scalable culling and optimization | Real spatial rejection and measured shader/submission/upload/shadow/resource improvements.                                                          | Candidates preserve implementation checks and report quality, timing and memory against matched baselines.                                  |
| EX07F — Final validation and delivery     | Final-code Debug/Release correctness, native performance, editor/native operation, inspected images and complete operating docs.                    | All EX07 gates pass together, with supported limits and no unexplained failures or quality reduction.                                       |

Current stage and item status live only in [tracker section 3.4](#stages-and-ownership).
This plan defines requirements; the audits preserve proof.

EX07-01–14 remain stable tracking IDs. Contracts and property review precede their
implementation; EX07-06/13 reference and instrument foundations start in EX07B,
and EX07-07's many-light scene and baseline close in EX07D. Round-trip/live editor
checks accompany repairs in EX07C; EX07F confirms final integration rather than
discovering missing transport for the first time.

Short exploratory profiles may guide review at any step but cannot be labelled
qualified baselines. EX07C may use a complete conservative list while spatial
rejection is optimized in EX07E; such a baseline must include every valid light
and requested shadow. If existing culling loses contributions, repair it first.
Freeze a baseline separately for each correctly rendered workload. A valid
unculled comparison preserves the measured benefit of introducing spatial culling;
never time a defective image as the acceptance reference.

## Exit artifacts

- Qualified physical references and per-view culling/image/lifetime/capacity tests.
- Canonical ABI/property inventory and tested caller-visible failure/recovery contracts.
- Workload/measurement manifest, native baseline/candidate timing and memory
  reports, accepted/rejected optimization decisions and actual supported limits.
- Inspected native captures of sparse/dense/moving/two-view and shadowed scenes,
  useful culling diagnostics, and the editor-authored input check. State the
  expected visible result before each launch. No unexplained warnings/errors.
- Updated LightingService/shader/publication/shadow documentation, relevant
  examples and operating commands; concise EX07 tracker closure linked to evidence.

EX07 closes only when calibration **and** many-light correctness/performance are
qualified. The previously accepted EX051 exposure operating point stays closed;
rerun its affected cases only if shared changes invalidate that evidence.

## Technical references

- [Clustered Deferred and Forward Shading](https://research.chalmers.se/en/publication/161725)
  motivates spatial assignment shared across rendering families; it does not
  establish Oxygen's performance or prescribe its implementation.
- [GPU occupancy guidance](https://gpuopen.com/learn/occupancy-explained/)
  supports profiling register pressure and spills rather than maximizing occupancy
  blindly. Use tools/counters appropriate to the actual adapter.
- [D3D12 GPU-based validation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)
  covers descriptor/lifetime checks and its instrumentation overhead.

## Supporting records

- [EX07A](EX07A/README.md)
- [EX07B](EX07B/README.md)
- [EX07C](EX07C/README.md)
- [EX07D](EX07D/README.md)
- [EX07E](EX07E/README.md)
- [EX07F](EX07F/README.md)

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
