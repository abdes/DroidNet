# ED-M10 - V0.1 Release Qualification

Status: `planned; qualification pending`

## 1. Purpose

Qualify the complete decided V0.1 capability matrix and operating envelope on a
matched artifact set. This is the release gate after ED-M07A/07B/08/09, not a
substitute for their implementation or a place to discover undecided scope.

## 2. PRD Traceability

All REQ-001 through REQ-042 and SUCCESS-001 through SUCCESS-009, as applicable to
the supported capability matrix; explicit non-goals remain excluded.

## 3. Required LLDs

The indexed V0.1 LLD set, especially standalone-runtime-validation, content-
pipeline, property-pipeline, documents-and-commands and viewport-and-tools.
PRD sections 8-10 own the workload, compatibility and release promises.

## 4. Scope

Matched-build manifest, portable 100-node / 1,000-logical-entry fixture,
end-to-end supported authoring/import/save/cook/preview/standalone/tool workflows,
failure safety, native mismatch handling, performance and lifecycle measurements.

## 5. Non-Scope

No larger-project guarantee, autosave/unsaved crash recovery, unsupported imports,
new settings/preset panel, deferred multi-viewport support, or relaxed parity
thresholds. No old milestone is retrospectively marked fully proven by new prose.

## 6. Implementation And Qualification Sequence

1. Produce/verify the matched Release artifact manifest; record source revision,
   configuration, schema IDs/hashes and runtime/tool hashes. Record machine/OS/
   driver/runtime details. Test missing and mismatched artifacts safely.
2. Generate the PRD fixture with exact logical counts, bounded visible triangles,
   hierarchy, primitive/imported geometry, shared/distinct scalar materials,
   camera, sun and environment. Retain sources, import options and fixture hashes.
3. From Project Browser, create/open, author, undo/redo, save/reopen, explicitly
   cook all supported scopes, inspect/validate/publish and run standalone parity.
   Include stale material/source behavior and the ED-M09 tool interactions.
4. From a copied project without derived directories, regenerate and compare
   stable identities, canonical descriptors and runtime semantics. No developer
   example-content paths or manual generated-file repair are allowed.
5. Run the document conflict/interrupted-save, cook transaction/recovery,
   cancellation, missing asset, runtime fault and stale-lifetime cases from the
   owning gap plans. Preserve the last valid saved/published state.
6. Measure the PRD budgets: 100 UI interactions, 100 warm queries, cold catalog/
   scene opening, 120 render warm-up frames plus 600 measured frames, busy-state
   feedback and 30 lifecycle cycles. Report raw samples and outstanding ownership
   counts. Larger-project experiments are labeled unqualified.

## 7. Project/File Touch Points

Existing project templates/content fixture generation, editor automation/test
entry points, qualified artifact manifest production, matched engine/cooker
install, ContentPipeline validation/report outputs and existing test projects.
Reports remain derived artifacts; do not create another status ledger.

## 8. Risks

Small smoke scenes, stale screenshots, mismatched binaries, helper-only tests,
manual file repair, hidden runtime overrides or relaxed thresholds cannot close
qualification. Use the recorded build/workload for repeatable results.

## 9. Validation Gates

- [ ] Every PRD-required capability has implementation and relevant automated/
  manual evidence, with no unsupported required field or unresolved scope item.
- [ ] ED-M07A/07B/08/09 gates pass and their evidence matches the qualified build
  or has documented applicable rerun evidence for changed paths.
- [ ] Portable regeneration and all promised save/cook failure guarantees pass.
- [ ] All PRD workload/interaction/render/lifecycle budgets pass on the recorded
  configuration; deviations remain failures, not implicit limit changes.
- [ ] User validates the full workflow and release evidence; saved/published
  identities and actual native observations are traceable.

## 10. Status Ledger Hook

Only after these gates pass, record the one ED-M10 row with build, fixture,
workflow and performance evidence. Preserve all historical milestone evidence;
new gap-closing results belong to their own milestones. No implementation,
visual, or release completion is implied by creating this plan.
