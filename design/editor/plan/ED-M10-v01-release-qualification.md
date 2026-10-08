# ED-M10 - V0.1 Release Qualification

Status: `planned; qualification pending`

## 1. Purpose

Qualify the complete decided V0.1 capability matrix and operating envelope on a
matched artifact set. This is the release gate after ED-M07A/07B/08, not a
substitute for their implementation or a place to discover undecided scope.

## 2. PRD Traceability

All REQ-001 through REQ-042 and SUCCESS-001 through SUCCESS-009, as applicable to
the supported capability matrix; explicit non-goals remain excluded.

## 3. Required LLDs

The indexed V0.1 LLD set, especially content-pipeline, property-pipeline, documents-and-commands and viewport-and-tools.
PRD sections 8-10 own the workload, compatibility and release promises.

## 4. Scope

Matched-build manifest, portable 100-node / 1,000-logical-entry fixture,
end-to-end supported authoring/import/save/cook/preview/tool workflows, loading
the cooked project in the standalone runtime,
failure safety and native mismatch handling. Editor performance is not
benchmarked in V0.1.

## 5. Non-Scope

No larger-project guarantee, autosave/unsaved crash recovery, unsupported imports,
new settings/preset panel, or the development-only qualification harness
(post-V0.1). No old milestone is retrospectively marked fully proven by new prose.

## 6. Implementation And Qualification Sequence

1. Produce/verify the matched Release artifact manifest; record source revision,
   configuration, schema IDs/hashes and runtime/tool hashes. Record machine/OS/
   driver/runtime details. Test missing and mismatched artifacts safely.
2. Generate the PRD fixture with exact logical counts, bounded visible triangles,
   hierarchy, primitive/imported geometry, shared/distinct scalar materials,
   camera, sun and environment. Retain sources, import options and fixture hashes.
3. From Project Browser, create/open, author, undo/redo, save/reopen, explicitly
   cook all supported scopes, inspect/validate/publish and load the cooked project in
   RenderScene. Include stale material/source behavior, the ED-M08 viewport
   tools and Content Browser relocation.
4. From a copied project without derived directories, regenerate and compare
   stable identities, canonical descriptors and runtime semantics. No developer
   example-content paths or manual generated-file repair are allowed.
5. Run the document conflict/interrupted-save, cook transaction/recovery,
   cancellation, missing asset, runtime fault and stale-lifetime cases from the
   owning gap plans. Preserve the last valid saved/published state.

## 7. Project/File Touch Points

Existing project templates/content fixture generation, editor automation/test
entry points, qualified artifact manifest production, matched engine/cooker
install, ContentPipeline validation/report outputs and existing test projects.
Reports remain derived artifacts; do not create another status ledger.

## 8. Risks

Small smoke scenes, stale screenshots, mismatched binaries, helper-only tests,
manual file repair or hidden runtime overrides cannot close
qualification. Use the recorded build/workload for repeatable results.

## 9. Validation Gates

- [ ] Every PRD-required capability has implementation and relevant automated/
      manual evidence, with no unsupported required field or unresolved scope item.
- [ ] ED-M07A/07B/08 gates pass and their evidence matches the qualified build
      or has documented applicable rerun evidence for changed paths.
- [ ] Portable regeneration and all promised save/cook failure guarantees pass.
- [ ] User validates the full workflow and release evidence; saved/published
      identities and actual native observations are traceable.

## 10. Status Ledger Hook

Only after these gates pass, record the one ED-M10 row with build, fixture and
workflow evidence. Preserve all historical milestone evidence;
new gap-closing results belong to their own milestones. No implementation,
visual, or release completion is implied by creating this plan.
