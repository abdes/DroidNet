# EX05.2 — Exposure code quality

Status: `validated`

| Field     | Summary                                                                 |
| --------- | ----------------------------------------------------------------------- |
| Outcome   | Agreed exposure quality fixes and affected Debug/Release qualification. |
| Remaining | None in the recorded scope.                                             |
| Evidence  | [Record below](#ex052--exposure-code-quality)                           |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Validated 2026-09-21.** The approved residual batch and Release include repair
are committed as `9ff39edcc` and `dc9ef824e`. Scoped changed code is tidy-clean;
65 selected Debug and 65 Release cases pass. A single matched I02 run confirms
performance/resource preservation and four byte-identical endpoint images.
The [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/checkpoint-manifest.json)
records raw references, retained diagnostics and reused evidence. No new warning
suppression was added. The following requirements governed the completed pass.

**Scope revised 2026-09-21 under the recorded protocol.** The authoritative residual
scope, reused evidence and change-specific checks are in
[Milestone record](#tasks-and-outcome).

1. **EX052-02 — Agree residual fixes.** Reuse the existing diagnostics and
   accepted source/test/performance checkpoints. Start with ExposurePass and
   PostProcessService; add another file only for a named exposure finding.
   Agree a finite list with concrete benefit, proposed change and affected checks.
   Refresh only stale/missing analysis; no new general inventory or baseline run.
2. **EX052-04 — Fix that list.** Keep changes coherent, warning-free and within
   existing contracts. A necessary API/ownership change requires interface and
   migration review before coding. Add tests only for real uncovered defects.
3. **EX052-10 — Validate affected code.** Use focused Debug checks while editing.
   At closure, compile the affected Release paths and run the union of required
   affected cases, reusing results still valid for final inputs. A full owning
   executable runs only when the shared change affects all its cases, once per
   required configuration. No automatic engine-wide or repeated per-item gates.
4. **EX052-12/GATE — Close.** Record the result and reused evidence, update owners
   and commit. An empty justified fix list closes through applicable existing
   proof; do not invent work to fill the slice.

Native fixture extraction/scenario splitting (`2c696c522`), the complete Vortex
test-quality review (`4f359ce2d`), and recording/binding migration with 589 Debug
and 403 Release checks (`22cea346b`) are completed inputs. EX052-05/06 are already
validated; the other redundant setup/restructuring/check tasks are merged into
the four steps above. Do not repeat those deliveries, sweep every test again,
change targets for cosmetic reasons, or experiment with `/bigobj` without a
real build problem. Preserve independent numerical oracles and existing filters.

No benchmark or visual campaign is mandatory for 5.2. Test-only and non-semantic
quality edits reuse accepted performance/output evidence. Changes that can alter
hot-path work, lifetime, synchronization, shader data or output need an explicit
impact decision and only the relevant matched check. The accepted CPU cost is
preserved; its original tighter targets and further optimization stay in the
later milestone. The 48-run GPU matrix and previous overhead/lifecycle campaigns
remain closed.

**Gate:** the agreed residual fixes are resolved, changed code is tidy-clean,
necessary affected checks pass, and existing contracts and accepted performance
are preserved.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 5.2 — Focused exposure quality | validated | Approved residual owner fixes and Release include repair committed; 65 Debug and 65 Release cases pass, scoped changed code is tidy-clean, and one matched I02 preservation run passes. | [Bounded scope and result](#tasks-and-outcome) |

## Tasks and outcome

**Slice status: validated, 2026-09-21.** The agreed residual batch is resolved;
02 -> 04 -> 10 -> 12/GATE is closed. Implementation commits are `9ff39edcc`
(owner quality) and `dc9ef824e` (approved blocking Release include repair).
The [checkpoint manifest](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/checkpoint-manifest.json)
is the single result record, including exact selected cases, commands, source
hashes, build logs, raw checks, reused evidence and the final closeout commit.

Scoped oxytidy resolves **151 of 166 owner findings**, with zero diagnostics on
changed code, zero hidden parse failures, and no added suppressions or check/filter
changes. The 15 unmodified residuals are nine literal ABI assertions, the
existing documented log-rate sentinel, two public enum-size suggestions and
three lease-copy suggestions. Their locations and dispositions remain in the
[quality result](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/quality-result.json).
The [include repair](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/include-repair-result.json)
restores Release-only dependencies; the benchmark body and its one pre-existing
lambda-style diagnostic remain unchanged. Unrelated existing Release build
warnings are retained in the logs, unchanged.

Both owning targets compile in Debug and Release. **10 service plus 55 native
exposure cases pass in each configuration**, serially with logging OFF; the
selection covers frame-slot/mask retention, seeds, source loss, failed/discarded
recording, range/filter/composition payloads, precision admission and conversion.
Each successful check ran once per configuration. The Release include repair
needed an incremental build retry, not another Debug/service test run. No new
regression test, discovery comparison or broad suite was warranted.

The [single matched I02 preservation result](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/preservation-result.json)
compares 7,200 Release frames at 1080p against the accepted 5.1 run. Added CPU
access guards triggered this check. Active CPU p95/p99 are **0.359673/0.458538 ms**
versus **0.514935/0.625473 ms**; GPU p95 is **6.641664 ms** versus **6.436864 ms**,
within the existing 5% threshold. GPU phase counts, recording/binding counts and
resource counts/placement match; all four endpoint files are byte-identical.
All 1,030 frozen inputs remain unchanged. This establishes preservation, not an
attributed optimization gain; original CPU goals remain deferred. The existing
48-run matrix, overhead campaign, fixture decomposition and broader test review
were reused, not repeated. Slice 6 is closed in [EX06](../EX06/README.md#tasks-and-outcome).

Correctness and benchmarks use separate executables:
`Oxygen.Vortex.Exposure.Tests` and `Oxygen.Vortex.Exposure.Benchmarks`.
The [benchmark README](../../../../../src/Oxygen/Vortex/Benchmarks/README.md) owns usage;
[split evidence](../../../../../out/build-ninja/analysis/vortex/exposure-benchmark-split/checkpoint-manifest.json)
and the [Vortex test quality result](../../../../../out/clang-tidy/vortex-test-quality/quality-summary-corrected.json)
retain the completed structural and test-review qualification.

**EX052-02 approved batch.** Starting source `e779d08bf` superseded the old
diagnostic locations. The scoped oxytidy refresh reports 166 warnings in the two
owner pairs, with complete analysis and no parse failures:
[raw report](../../../../../out/clang-tidy/ex052/run-20260921-090650-a4oadpqt/summary.json).
The review approved these bounded groups before implementation:

| Finding and live owner                                                               | Change and concrete benefit                                                                                                                                                                          | Affected verification and stop                                                                                                                                                                  |
| ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Partial records and member initialization in both owners                             | Explicit defaults and designated initialization make empty state, optional inputs and ownership identities unambiguous. Preserve existing initialized values and layouts.                            | Compile actual consumers; existing service settings/capture/publication and native transition/source-loss cases. Stop at explicit, warning-clean initialization.                                |
| Array and optional access in both owners                                             | Use compile-time element access for fixed words and checked access for runtime frame/product indices and required pipeline/seed values. Preserve valid-path values, ordering and lifetime retention. | Existing frame-slot retirement, filter-gradient retry, precision/producer ordering, seed and source-loss cases in Debug/Release. Stop when selected accesses are safe and diagnostics resolved. |
| Repeated shader control values in ExposurePass and product identities in both owners | Name the existing controls and identities without changing their numerical values, constant-buffer layout or shaders.                                                                                | Source-to-shader value review plus affected gradient, composition, admission and conversion cases. Stop at equivalent payloads and dispatches.                                                  |
| Local cast/initializer/return and private nodiscard diagnostics                      | Apply local modern C++ corrections without changing public interfaces or ownership.                                                                                                                  | Scoped tidy and affected compilation; reuse valid behavioral proof for non-semantic changes.                                                                                                    |

The review also accepted the one-file Release include repair in
`Benchmarks/ExposureOverhead_bench.cpp` (then under `Test/Exposure`) after the required build
exposed missing types/functions in its NDEBUG-only body. Restore direct headers
under the same guard; keep behavior, test identities and its existing disabled
status. Use Release-configured oxytidy and incremental Release compilation.
Existing Debug and Release service checks remain applicable to this include-only
change. The original failed build and the successful retry are both retained.

Public enum sizes, lease-copy optimization and literal ABI assertions remain
outside this approved batch. Record their residual diagnostics; do not suppress
them or describe the entire owners as warning-free. No new regression test is
planned unless implementation reveals a genuinely uncovered defect. Run the
union of affected checks once per required configuration after coherent edits;
only a demonstrated output/performance impact triggers a matched runtime check.

#### Completed work to reuse

| Delivered work                                       | Existing source/evidence                                                                                                                                                                                                                                                                                           | Consequence for 5.2                                                                                                                                                                                                                                |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Native fixture extraction and scenario decomposition | `2c696c522`; [current layout and ownership](../../../../../src/Oxygen/Vortex/Test/Exposure/README.md), `Test/Exposure/Fixtures`, `Benchmarks` and the existing ExposureGpu CMake target                                                                                                                            | EX052-05/06 are already delivered. Keep the current fixture owners, executable, filters and opt-in workloads. Do not split or merge them again.                                                                                                    |
| Complete Vortex test-quality review                  | `4f359ce2d`; [corrected review](../../../../../out/clang-tidy/vortex-test-quality/quality-summary-corrected.json) and [file ledger](../../../../../out/clang-tidy/vortex-test-quality/review-ledger.json): 156 files, 129 TUs plus 21 headers, zero diagnostics at that checkpoint, 876 Debug checks plus LinkTest | No repeat directory-wide test review, initializer sweep, suppression campaign or 47-target baseline run. Check subsequent changes against the later evidence below.                                                                                |
| Recording/binding API and caller migration           | `22cea346b`; [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-corrections13/checkpoint-manifest.json): 589 Debug and 403 Release owning checks; 89-TU analysis with zero diagnostics on changed code                                                                    | Reuse lifecycle, failure/retry, publication, binding and queued-reader coverage. No new API migration or owning-suite run merely because 5.2 starts. This is changed-code cleanliness, not a claim that every existing owner file is warning-free. |
| Accepted performance and output baseline             | [final 5.1 checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/checkpoint-manifest.json) and [CPU decision](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-corrections13/decision-table.json)                                      | Preserve the user-accepted operating point. Do not reopen CPU targets, repeat the 48-run matrix, H1-H5/R091, overhead campaign or MultiView captures.                                                                                              |

#### Remaining scope and admission rule

Start with **ExposurePass and PostProcessService implementation/header pairs**.
The existing [89-TU report](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-corrections13/tidy-final/summary.json)
is the input; its raw logs already report 232 and 61 diagnostics respectively in
the two implementation files. These are untriaged findings, not 293 proven bugs
or an instruction to rewrite both files. The report's 1,733 diagnostics across
all encountered files are not the 5.2 backlog.

For each proposed fix, record one short entry: **live location/diagnostic,
concrete defect or maintenance benefit, intended change, affected tests, and
stopping condition**. Confirm that the current code still contains the issue.
Use the existing source hashes, compile settings and reports to identify stale
or missing evidence; refresh only affected TUs, not the entire historical scope.
Group repeated instances of the same fix within an owner. There is no separate
baseline-capture, broad inventory or general design phase.

Admit work that fixes a confirmed correctness/lifetime problem, an actionable
compiler/tidy finding in the agreed code, or duplication/control flow that
obscures an actual invariant or error path. File length, warning count and a
preferred layout alone do not justify a refactor. Any helper extraction must
simplify the agreed fix; it is not an independent deliverable. Do not manufacture
one-use constants or layers merely to move a warning elsewhere.

Related SceneRenderer, SceneTextures, Environment, Graphics, test or tooling
files enter only when a named finding in an exposure contract needs that change.
Agree additions before editing. No directory-wide cleanup of these domains, no
blanket PostProcess/deferred/offscreen test review, no generic fixture framework,
no unconditional HLSL/Python audit, and no `/bigobj` removal experiment. Preserve
existing numerical oracles, test identities, shader/root ABI, resource ownership
and accepted runtime behavior. Further CPU optimization and Slice 6 features
remain outside this slice. Proposed API/ownership changes require the user's
review of the actual interface and caller migration before coding; no wrappers
or parallel legacy APIs.

Use the established `tools/cli/oxytidy.ps1` / `oxytidy.py` workflow, with the
explicit build directory and real compile database. Keep repository checks and
filters unchanged. Fix changed/added code without clang-tidy warnings; do not
hide parse failures or add blanket suppressions. A vital narrow exception needs
its exact diagnostic, location, justification and rejected alternatives. Existing
out-of-scope diagnostics remain recorded, not silently described as fixed.

#### Validation follows the change

Choose tests when agreeing the fix, before editing. Existing results remain
applicable only after checking relevant production/test dependencies, build
configuration, compiler flags and shader inputs for changes. Reuse unchanged
proof by reference; do not rehash/copy every historical payload or invent a new
validation framework.

| Change                                                                                        | Smallest verification                                                                                                                          | Closure requirement                                                                                                                                                                                                                                                          |
| --------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Documentation/comments/formatting only                                                        | Existing hooks and diff review; check references when edited                                                                                   | No C++ build, native test, discovery or performance run.                                                                                                                                                                                                                     |
| Local non-semantic C++ cleanup                                                                | Scoped tidy and compile the affected target; review that types, evaluation order, ownership and generated work are preserved                   | No new regression test or automatic full suite. Reuse applicable correctness/performance proof.                                                                                                                                                                              |
| Executable logic, error path or resource lifetime                                             | Existing named Debug cases exercising the affected path and relevant failure/retry boundary; add a regression only for a real uncovered defect | Compile affected Release paths once and run the corresponding affected Release cases on final inputs. Expand only if the dependency/call-path review shows broader impact.                                                                                                   |
| Shared fixture, widely used owner/header or caller contract                                   | Compile actual consumers; select every affected case. Run discovery comparison only if registration, identity or target wiring changes         | An entire owning executable is justified when its shared setup/contract affects all its cases. Run it once per required configuration on the final checkpoint, not once per edit or task.                                                                                    |
| Hot-path work, allocations, synchronization, shader-visible data or image output could change | First determine whether the proposal belongs to the deferred performance milestone or needs separate approval                                  | If retained in 5.2, agree the affected recipe/output check up front. Reuse a comparable accepted baseline and collect only the required candidate at closure; a fresh pair needs a concrete comparability reason. No matrix, overhead campaign or automatic visual/demo run. |

Batch a coherent owner change before building. Run tidy early on that changed
scope, then focused Debug checks; fix failures from their evidence and rerun only
the affected checks. At closure, use the **union of affected cases once per
required configuration**, minus results already valid for the final relevant
inputs. Item completion, a commit, or a documentation edit does not invalidate a
passing result. A later source/configuration change invalidates only dependent
results. Do not automatically rerun 876, 589, 403 or all 228 exposure cases.
A failure may justify expansion; record the reason rather than starting another
full campaign. Native jobs remain serial and logging OFF.

If a broad native batch is actually justified, use one execution-only validation
subagent with exact commands, controls, input identities and expected outputs.
Freeze runtime inputs and do read-only review or draft notes while it runs; no
concurrent builds, GPU work, commits or stash/restore. No subagent or frozen-run
manifest is needed for documentation or a short local check.

Performance preservation means the accepted 5.1 operating point, including the
explicit CPU disposition, not meeting the original unachieved CPU targets.
For a justified matched comparison, investigate p95 growth above
`max(0.05 ms, 5% of baseline)` or material p99/memory regression; follow the
existing bounded sampling/retry protocol. Reuse numerical and visual evidence
when the change cannot affect their output. No benchmark is an automatic 5.2
deliverable.

#### Completed tasks

| ID         | Work item                        | Status    | Depends on        | Delivery and stopping condition                                                                                                                                                                                                                     |
| ---------- | -------------------------------- | --------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EX052-02   | Agree the residual fix list      | validated | Closed EX051-GATE | Review approved the finite four-file owner batch and subsequently the named Release-only include repair. Existing findings were reconciled against e779d08bf; no broad baseline was repeated.                                                       |
| EX052-04   | Implement agreed fixes           | validated | 02                | Implemented and committed as 9ff39edcc and dc9ef824e. Explicit record/descriptor initialization, checked access, named unchanged shader controls/layouts and direct Release dependencies; no public signature/ABI or warning-policy change.         |
| EX052-10   | Validate the final affected code | validated | 04                | Scoped changed code is clean; 65 Debug and 65 Release selected checks pass. One matched I02 candidate preserves accepted CPU/GPU cost, resources and four byte-identical endpoints. Exact evidence and reuse decisions are in the checkpoint above. |
| EX052-12   | Close the residual-quality pass  | validated | 10                | Tracker, package plan, main plan and PostProcess owner are reconciled to the single checkpoint and raw references. Implementation and documentation closeout commits are recorded in that checkpoint.                                               |
| EX052-GATE | Focused quality acceptance       | validated | 02, 04, 10, 12    | All agreed fixes are resolved and necessary validation passes. Existing behavior and accepted operating point are preserved; explicitly excluded diagnostics remain recorded.                                                                       |

#### Disposition of the original task IDs

| Original ID | Status     | Revised disposition                                                                                                                                                        |
| ----------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EX052-01    | superseded | Baseline/scope reuse is part of 02; existing 5.1 and test-quality records replace a new freeze campaign.                                                                   |
| EX052-03    | superseded | Concrete fix/API agreement is part of 02. No separate suite-restructuring proposal.                                                                                        |
| EX052-05    | validated  | Shared native fixture owners already delivered in `2c696c522`, hardened in `4f359ce2d`, and qualified again where affected by `22cea346b`; evidence above.                 |
| EX052-06    | validated  | Scenario and benchmark TUs already split and wired to the existing target; current native gate is 228 enabled cases, eight opt-in cases disabled. No repeat decomposition. |
| EX052-07    | superseded | A structural/tooling change is allowed only as a necessary part of an agreed 04 fix; no general audit deliverable.                                                         |
| EX052-08    | superseded | Useful readability/tidy changes belong to 04's finite fix list, not a second cleanup pass.                                                                                 |
| EX052-09    | superseded | Compile/discovery work is conditional in 10; no automatic regeneration, wrapper rewrite or `/bigobj` experiment.                                                           |
| EX052-11    | superseded | Performance/output preservation is conditional in 10; no separate timing or visual campaign.                                                                               |

## Residual-quality result

**Validated 2026-09-21; implementation `9ff39edcc`, include repair `dc9ef824e`.**
The [tracker result](#tasks-and-outcome)
and [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/checkpoint-manifest.json)
own the selected checks, raw evidence and excluded diagnostics.

ExposurePass constructs suitability and composition constants through named
local records matching the existing HLSL word order. Both remain 128 bytes and
publish through the existing raw-word publisher; size and shared interpretation
offsets are asserted. Product identities and control bits retain their wire
values. Fixed low/high identity words use compile-time element access; runtime
frame/product indices and required pipeline descriptors are checked before use.
The service validates an optional seed locally and explicitly initializes
partial input/status records and prepared ownership identities. Public function
signatures, GPU/shader layouts, submission publication, lifetime retention and
independent numerical oracles are preserved.

The approved Release-only benchmark repair restores direct includes without
changing its body or enabled/disabled identity. Changed code has no clang-tidy
warnings or added suppressions. The affected 65 cases pass in each configuration;
one matched I02 run preserves the accepted operating point and byte-identical
endpoint output. Broader completed gates are reused; this quality pass does not
claim the deferred CPU optimization targets or begin Slice 6.
