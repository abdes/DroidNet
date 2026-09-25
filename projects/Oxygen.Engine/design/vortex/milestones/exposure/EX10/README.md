# EX10 — Exposure package closeout

Status: `validated`

| Field     | Summary                                                                      |
| --------- | ---------------------------------------------------------------------------- |
| Outcome   | Final retained requirement coverage, acceptance and operating documentation. |
| Remaining | None in the recorded scope.                                                  |
| Evidence  | [Validation record](validation.md)                                           |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Dependency:** EX09. **Tracked by:** EX10-03–08/GATE; EX10-01–02 are removed.

Map retained requirements to applicable existing proof or focused new results.
Record relevant source/build/shader/scene identities and numerical tolerances in
a durable Markdown summary using existing test outputs. Run affected final
Debug/Release checks; do not repeat unchanged captures, benchmarks or whole suites
without a reason. Include user UI acceptance and working application/test commands.
Reconcile owner docs and tracker. No new acceptance platform, runner or schema.

**Exit gate:** no unresolved retained product requirement or discovered defect;
automated evidence, acceptance and operating instructions are recorded.
Removed infrastructure stays removed. Reactivated EX08.2 must qualify its named
workflows before EX10 can close; it is not covered by earlier native-only checks.

## Package acceptance criteria

Checked foundations are validated by EX02-07 in the tracker. EX07's engine gates
and final interactive editor acceptance are complete. Remaining
unchecked items belong to the later steps and do not reopen those foundations.
Package closure remains dependent on final integrated evidence.

- [x] Fixed/manual-camera/Auto/disabled exposure use one consistent state contract.
- [x] Hybrid EV/s adaptation, masks, curves, black handling and zero target work.
- [x] All lifecycle and shared/stateless policies are implemented and tested.
- [x] Every active HDR path uses frame-pinned P and final S/P consistently.
- [x] Bootstrap and numerical recovery preserve valid bright/dark metering signals.
- [x] Authored fields round-trip through all active persistence surfaces (EX06).
- [x] Directional, point and spot reference units and material expectations pass.
- [x] EX07 many-light correctness, supported capacities and measured performance/improvement gates pass.
- [x] EX07 final editor interaction sign-off; user confirmed creation, live light edits, undo/redo and save/reopen persistence.
- [x] LightBench is a properly repaired, visually useful exposure benchmark;
      retained reference/presets/transition workflows pass focused numerical and user UI acceptance.
- [x] MultiView succeeds visually in ordinary and proof layouts; multiple views
      do not break exposure, and exposure changes do not break rendering/composition.
- [x] Retained renderer requirements have applicable existing proof or focused passing new checks.
- [x] Post-processing console tests and user UI checks pass (EX08.1); EX08.2 widget automation and build isolation pass in Debug/Release.
- [x] Owning documents and operational instructions describe the implemented behavior.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 10 — Package closeout | validated | Evidence, acceptance and operating docs reconciled. Closeout committed in `28fc6fba4`. | [EX10 closeout](validation.md) |

## Tasks and outcome

**Validated and closed.** [The closeout report](validation.md) records
applicable proof, final acceptance and review entry points. Structured delivery is complete; commits are listed in [EX10 closeout](validation.md).

| ID        | Required result / disposition                                                                                             | Delivery step | Status    |
| --------- | ------------------------------------------------------------------------------------------------------------------------- | ------------- | --------- |
| EX10-01   | Shared interactive/batch recipe engine removed; shared canonical app/test scene definition retained in EX08-07.           | —             | removed   |
| EX10-02   | General measurement/batch runner and result schema removed; existing numerical output and standard UI-test JUnit suffice. | —             | removed   |
| EX10-03   | Applicable native non-DemoShell lifecycle/sharing/recovery evidence and focused gap repair.                               | EX09C         | validated |
| EX10-04   | Existing MultiView scripts retain structural/numerical checks; no new measurement/schema integration.                     | EX09E         | validated |
| EX10-05   | Existing isolation/sharing/reorder/resize/lifetime/mode proofs credited or affected checks rerun.                         | EX09E         | validated |
| EX10-06   | Durable Markdown with relevant build/shader/scene identities, tolerances, test results and prior-evidence applicability.  | Each step     | validated |
| EX10-07   | Affected final Debug/Release checks and user UI acceptance; no repeat capture/benchmark campaign.                         | EX10          | validated |
| EX10-08   | Owner docs, both operating READMEs, plan and tracker reconciled.                                                          | EX10          | validated |
| EX10-GATE | No unresolved retained product requirement/defect; evidence, acceptance and instructions recorded.                        | EX10          | validated |

## Supporting records

- [validation](validation.md)
