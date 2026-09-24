# EX07A completion audit

Status: **EX07A validated on 2026-09-23. Overall EX07 remains in_progress.**

Current progression is recorded in the [tracker](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items):
EX07D is closed with the [baseline register](EX07D-baseline-report.md); EX07E is
prepared and awaits the user's explicit start signal. The audit below preserves
A's evidence and stage ownership at its closure.

The [six-step EX07 plan](EX07-lighting-correctness-and-scalability.md#six-ordered-implementation-steps)
and its contract/property deliverables define the scope. This audit does not
replace them or move unresolved implementation into a new exclusion. A requires
the agreed contract, canonical record/interface migration and native ABI proof.
The independent reference qualification belongs to B; physical/property/ingress
repairs and full failure/lifetime qualification belong to C; measured baselines,
optimization and final integration belong to D–F.

## Requirement-to-evidence audit

| Requirement                                                         | Authoritative evidence inspected                                                                                                                                                     | Result                                                                                                                                                                                                                                       |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Finalized review decisions and documentation committed              | `e910bd554`; [D1–D6](EX07A-contract-review.md#approved-decisions), LightingService/PBR owners                                                                                        | Passed. No unresolved product choice; numerical/model qualification is not inferred from approval.                                                                                                                                           |
| Complete authored-property review and strict migration obligations  | [LP01–LP32 inventory](../lld/lighting-properties.md), ingress-owner map, validation/invalidation keys, scene-v7 offsets and suite ownership                                          | Contract defined. Existing source/packed/editor/script deficiencies remain explicit C repairs, including obsolete attenuation/sun fields.                                                                                                    |
| One canonical CPU/HLSL wire contract                                | [ABI tables](../lld/lighting-gpu-abi.md); 15 production payloads in the native decode shader, matching C++ layout assertions                                                         | Implemented and native-tested. No second positional-light payload, embedded-primary lighting header or old local shadow record remains in the Vortex path.                                                                                   |
| Actual upload/decode/readback proof, not same-size assertions alone | `Test/Lighting/{LightGridAbi,LightEvaluationAbi,ShadowRecordAbi,LightGridLookup}_test.cpp`; `Test/Shaders/LightingGpuAbiProbe.hlsl`                                                  | 22 native decode/lookup cases plus the CPU index-contract check pass Debug/Release, including adjacent/nonzero records, high-bit words, sentinels, reserved fields, matrix transforms and the changed-upload-lane negative control.          |
| Strong index types and symbolic invalid values                      | `Types/LightingIndices.h`, CPU record members, native sentinel tests                                                                                                                 | Implemented. Node handles stay CPU-side; selection indices address immutable GPU arrays.                                                                                                                                                     |
| Source identity, directional array and shadow ownership             | SceneRenderer selection; `ShadowReferenceBuilder`; immutable shadow-reference attachment; directional deferred/forward/fog captures                                                  | Implemented foundation. All selected directionals retain their identity; shadow indices come from produced records. Full source mutation/ingress qualification remains C.                                                                    |
| Content-relative grid semantics from actual producer parameters     | `LightGridBuilder`, `LightCullingConfig`, production `ClusterLookup.hlsli`, native lookup tests                                                                                      | Repaired and tested. CPU-produced span/curve/scale values pass actual D3D12 near/far and 170 interior/outside checks. The old implementation failed all three far-plane cases. Live forward capture verifies the published encoding.         |
| Unambiguous preparation identities and atomic rejection             | New LightingService tests for zero scene generation, duplicate/invalid view IDs and recovery; negative control fails both new tests                                                  | Repair implemented; final configuration results are recorded in the identity-admission checkpoint. Scene-less empty publications and view ID zero remain valid.                                                                              |
| No leftover compatibility interfaces in migrated paths              | Deleted positional/culler contracts, singleton sun/AP accessors, obsolete atmosphere shadow-authority fields and transient-upload compatibility state; canonical diagnostic decoders | Those migrations are verified. The unused directional `source_radius` angle copy and local explicit padding are removed; atmosphere angular size and local finite-emitter radius retain their owning paths.                                  |
| Numeric capacity policy backed by actual requirements               | D1/D6; versioned `ShadowAllocationRequirements_test.cpp`; `allocation-requirements-current.json`                                                                                     | Freeze evidence passes. Both configurations reproduce 18 D32S8/D32 queries on the RTX 3080. This does not establish a runtime admission ledger, resident frame peak or timing budget.                                                        |
| Scheduling, failure/recovery and fence-lifetime contract            | [LightingService sections 3–4](../lld/lighting-service.md#3-gpu-execution-and-synchronization), review scheduling sequence and fault obligations                                     | Contract defined. Complete lists are the active baseline. GPU spatial culling, sticky GPU failure/output gating and all delayed/discarded-submission outcomes are not qualified by the current baseline.                                     |
| Required capture and human evidence                                 | Recent directional/local/fog/AP/readability reports; [flicker validation](EX07A-offscreen-flicker-validation.md); user-confirmed local-light repair                                  | Applicable checkpoint evidence exists. It is scoped to the observed behavior, not full photometric/BRDF or performance qualification.                                                                                                        |
| Final coherent closure record                                       | This audit, owning status/LLDs, complete final-code results and catalog checks                                                                                                       | Passed. The source audit checks every declared member of all 15 wire records, all 32 inventory IDs and retired interfaces; four catalog tests pass in each configuration. Current evidence and later-stage obligations are reconciled below. |

The current 24-case native test executable contains 22 D3D12 decode/lookup
cases, one D3D12 allocation query and one CPU index-contract case. Current
reproducible results are under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07a`:

- `grid-depth-LightingGpuAbi-{debug,release}.json`: all 24 cases per configuration.
- `closure-catalog-{debug,release}.json`: four catalog checks per configuration.
- `closure-contract-source-audit.json`: record/field coverage, source hashes,
  property IDs, removed interfaces and closure disposition.
- `allocation-requirements-current.json`: reproducible D32S8/D32 allocation matrix.
- `shadow-owner-{deferred,forward,fog,local}-report.txt`: canonical runtime routing.
- `ap-interface-report.txt`: 72 AP draws / 1,152 pixels; maximum error 5.96e-8.
- `readability-canonical-verdict.json`: original materials, identical HDR inputs
  across the meter-profile change and rejected white negative controls.
- `transient-cleanup-*-{debug,release}.json`: multi-allocation and same-frame
  descriptor retention plus SceneRenderer integration.

These files were inspected during the audit. The lost historical standalone
depth-format probe is explicitly identified in the
[memory review](EX07-shadow-memory-review.md#native-evidence-and-limits); it is
not substituted for a current reproducible rendering gate.

## Disposition and handoff

A's frozen contracts, canonical record/interface migration, native ABI evidence,
applicable captures and user checkpoints pass their scoped exit gate. The
`grid-depth-checkpoint.json` records the last production-source repair and its
276 Debug/Release test executions. The subsequent catalog tests add eight
executions; source/document reconciliation changes no production behavior.

Proceed to EX07B: independently qualified physical/BRDF/finite-source references,
known-input GPU probes, deterministic image/reference fixtures and bounded
instrumentation. Keep EX07-01–14 and EX07-GATE open according to their owning
scopes. In particular, A does **not** qualify the final BRDF, finite-emitter/wide-
spot support, strict scene-v7/editor/script migration, dynamic memory admission,
all failure/lifetime paths, spatial culling or measured performance. These remain
required work in B–F, not new exclusions. The overall goal stays active.
