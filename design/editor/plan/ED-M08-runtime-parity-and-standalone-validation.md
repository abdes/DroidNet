# ED-M08 - Runtime Parity And Standalone Validation

Status: `planned; implementation and validation pending`

## 1. Purpose

Prove saved/published editor content in embedded and standalone runtime using
exact project output, native observations, and controlled images. Required
predecessors: ED-M02's supported viewport evidence and ED-M07A/07B. ED-M09 tools
are not a prerequisite; overlays are disabled for parity.

## 2. PRD Traceability

REQ-018/019/022-026/030/037/039-042; SUCCESS-001/003/004/006.

## 3. Required LLDs

[standalone-runtime-validation.md](../lld/standalone-runtime-validation.md),
[runtime-integration.md](../lld/runtime-integration.md),
[content-pipeline.md](../lld/content-pipeline.md),
[environment-authoring.md](../lld/environment-authoring.md).

## 4. Scope

Exact validation request, UI launch, published-output lease, expected saved-state
snapshot, native observations/captures, semantic/image comparison, diagnostics
and qualification artifacts for the PRD workload and field suite.

## 5. Non-Scope

No fallback to engine example content, fuzzy scene lookup, synthetic sun/material,
editor-owned runtime loading, generic validation dashboard, or headless visual
parity claim. No M04 closure sweep or M09 tool dependency.

## 6. Implementation Sequence

1. Add the specified version-1 request/result DTOs and managed coordinator in
   ContentPipeline. Capture expected state and source-to-cooked identity mapping
   from the published saved input, hash artifacts, acquire the output lease.
2. Add `--editor-validation-request` to RenderScene, bypassing normal example
   startup selection/restored state. Load exact requested roots and scene via
   engine APIs, validate artifact fingerprints and produce native observations.
3. Add controlled embedded/native rendered captures through runtime capabilities,
   explicit camera selection and scene-frame timing. Preserve the user's view
   and authored state; do not confuse queue acceptance with a captured image.
4. Implement semantic/image comparisons and auto-exposure cases using the fixed
   tolerances in the LLD. Missing artifacts/fields fail; native exit zero alone
   does not pass. Retain original captures and field mismatch pointers.
5. Wire Validate in Standalone and cancellation into the existing scene commands
   and result/output surfaces. Child termination/I/O drain releases the output
   lease on every path; only the launched child is owned by this workflow.

## 7. Project/File Touch Points

ContentPipeline validation coordinator/contracts/tests; WorldEditor scene menu/
commands; Runtime/Interop observation and frame-capture capabilities;
`Oxygen.Engine/Examples/RenderScene/main_impl.cpp`, MainModule and reusable
DemoShell scene-loading/capture code. Native engine public APIs own observations.

## 8. Risks

False positives from restored example content, request-echo observations, hidden
render overrides, mismatched timing/exposure, stale publications and dropped
fields are explicit rejection cases. Native changes require engine-owner build
and validation; no implementation proof is inferred from this plan.

## 9. Validation Gates

- [ ] Exact project/scene load works with engine example content unavailable.
- [ ] Missing/mismatched request/build/root/asset fails before unsafe use.
- [ ] Every required field passes observed-state comparison after save/cook/load.
- [ ] Base static images and exposure/field cases pass the specified tolerances.
- [ ] Cancel, timeout, child crash, source edits during validation and attempted
  publication during the output lease preserve state and report precise results.
- [ ] The user validates the complete artifact set for the qualified build.

## 10. Status Ledger Hook

Record one ED-M08 row with request/publication/build/fixture identities and
semantic/image results after all gates pass. Earlier cook/viewport evidence does
not close this milestone. ED-M09 may then build on the qualified runtime path.
