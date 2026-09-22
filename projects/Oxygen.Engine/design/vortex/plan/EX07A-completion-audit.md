# EX07A completion audit

Status: **in_progress; completion is not established.** Updated 2026-09-23.

The [six-step EX07 plan](EX07-lighting-correctness-and-scalability.md#six-ordered-implementation-steps)
and its contract/property deliverables define the scope. This audit does not
replace them or move unresolved implementation into a new exclusion. A requires
the agreed contract, canonical record/interface migration and native ABI proof.
The independent reference qualification belongs to B; physical/property/ingress
repairs and full failure/lifetime qualification belong to C; measured baselines,
optimization and final integration belong to D–F.

## Requirement-to-evidence audit

| Requirement                                                         | Authoritative evidence inspected                                                                                                                                                     | Result                                                                                                                                                                                                                                                                                   |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Finalized review decisions and documentation committed              | `e910bd554`; [D1–D6](EX07A-contract-review.md#approved-decisions), LightingService/PBR owners                                                                                        | Passed. No unresolved product choice; numerical/model qualification is not inferred from approval.                                                                                                                                                                                       |
| Complete authored-property review and strict migration obligations  | [LP01–LP32 inventory](../lld/lighting-properties.md), ingress-owner map, validation/invalidation keys, scene-v7 offsets and suite ownership                                          | Contract defined. Existing source/packed/editor/script deficiencies remain explicit C repairs, including obsolete attenuation/sun fields.                                                                                                                                                |
| One canonical CPU/HLSL wire contract                                | [ABI tables](../lld/lighting-gpu-abi.md); 15 production payloads in the native decode shader, matching C++ layout assertions                                                         | Implemented and native-tested. No second positional-light payload, embedded-primary lighting header or old local shadow record remains in the Vortex path.                                                                                                                               |
| Actual upload/decode/readback proof, not same-size assertions alone | `Test/Lighting/{LightGridAbi,LightEvaluationAbi,ShadowRecordAbi,LightGridLookup}_test.cpp`; `Test/Shaders/LightingGpuAbiProbe.hlsl`                                                  | 21 ABI/lookup cases pass Debug/Release, including adjacent/nonzero records, high-bit words, sentinels, reserved fields, matrix transforms and the changed-upload-lane negative control.                                                                                                  |
| Strong index types and symbolic invalid values                      | `Types/LightingIndices.h`, CPU record members, native sentinel tests                                                                                                                 | Implemented. Node handles stay CPU-side; selection indices address immutable GPU arrays.                                                                                                                                                                                                 |
| Source identity, directional array and shadow ownership             | SceneRenderer selection; `ShadowReferenceBuilder`; immutable shadow-reference attachment; directional deferred/forward/fog captures                                                  | Implemented foundation. All selected directionals retain their identity; shadow indices come from produced records. Full source mutation/ingress qualification remains C.                                                                                                                |
| Content-relative grid semantics from actual producer parameters     | `LightGridBuilder`, `LightCullingConfig`, production `ClusterLookup.hlsli`, native lookup tests                                                                                      | **Open.** Existing perspective boundary tests use hand-authored B/O/S values. The producer still pads far depth and offsets/clamps near depth; its own parameters have not been proved against the frozen near/far boundary contract.                                                    |
| Unambiguous preparation identities and atomic rejection             | New LightingService tests for zero scene generation, duplicate/invalid view IDs and recovery; negative control fails both new tests                                                  | Repair implemented; final configuration results are recorded in the identity-admission checkpoint. Scene-less empty publications and view ID zero remain valid.                                                                                                                          |
| No leftover compatibility interfaces in migrated paths              | Deleted positional/culler contracts, singleton sun/AP accessors, obsolete atmosphere shadow-authority fields and transient-upload compatibility state; canonical diagnostic decoders | Those migrations are verified. **Open source review:** the CPU directional `source_radius` still duplicates an atmosphere angle without a consumer, and the local selection retains unused explicit padding. Resolve their purpose/removal before claiming the interface audit complete. |
| Numeric capacity policy backed by actual requirements               | D1/D6; versioned `ShadowAllocationRequirements_test.cpp`; `allocation-requirements-current.json`                                                                                     | Freeze evidence passes. Both configurations reproduce 18 D32S8/D32 queries on the RTX 3080. This does not establish a runtime admission ledger, resident frame peak or timing budget.                                                                                                    |
| Scheduling, failure/recovery and fence-lifetime contract            | [LightingService sections 3–4](../lld/lighting-service.md#3-gpu-execution-and-synchronization), review scheduling sequence and fault obligations                                     | Contract defined. Complete lists are the active baseline. GPU spatial culling, sticky GPU failure/output gating and all delayed/discarded-submission outcomes are not qualified by the current baseline.                                                                                 |
| Required capture and human evidence                                 | Recent directional/local/fog/AP/readability reports; [flicker validation](EX07A-offscreen-flicker-validation.md); user-confirmed local-light repair                                  | Applicable checkpoint evidence exists. It is scoped to the observed behavior, not full photometric/BRDF or performance qualification.                                                                                                                                                    |
| Final coherent closure record                                       | This audit, owning status/LLDs, complete final-code results and catalog checks                                                                                                       | **Open** until the remaining producer-parameter/interface review and final reconciliation pass.                                                                                                                                                                                          |

The 22nd native case is the allocation query; it is not another ABI decoder.
Current reproducible results are under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07a`:

- `allocation-refresh-{debug,release}.json`: 22 native cases per configuration.
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

## Next actions

1. Qualify grid depth boundaries using the actual CPU-produced parameters in the
   production HLSL lookup; repair disagreements and unrepresentable encodings.
2. Finish the identified CPU selection-interface cleanup without changing the
   separately owned atmosphere disk or local finite-emitter contracts.
3. Reconcile all A evidence and current status after those changes. Do not mark A
   complete merely because its wire decoder suite passes.
