# ED-M07B workflow audit

Date: 2026-09-15

This audit maps the 19 journeys in `content-cooking-workflows.md` section 7 to
implemented paths and recorded results. Tests were inspected for their actual
scope; native state checks, rendered controls, fault injection and user feedback
are identified in the linked records.

| Required journey | Completion evidence |
| --- | --- |
| Create/save/discover/assign a material before and after cooking | [Workspace publication](ED-M07B-workspace-publication.md): real document creation, queued automatic work, pre-cook picker state, mounted native material and unchanged scene history. |
| Import glTF/FBX, choose named outputs and assign | [Typed assets](ED-M07B-typed-assets.md) and [import dialogs](ED-M07B-import-dialog-journeys.md): actual review controls, native output identities, geometry/material picker commands and native bindings. |
| Collision, unsupported source and dirty reimport target | Native dialog cancellation/replacement cases; pipeline tests `UnsupportedSourceReportsOriginalLocation`, `DirtySourceReturnsSaveRequirementWithoutRetention`, `ReplacementConflictsPreserveSourceAndOutput` and `ReimportWithChangedAssetNamesPreservesPublishedOutput`. The 449-case pipeline run passes. |
| Browse/filter/pick with 1,000 entries without cooking | [Browser workload](ED-M07B-browser-workload.md) and [navigation](ED-M07B-browser-navigation.md): real-catalog timing, stable focus/selection, combined type filters and no browse-triggered cook. |
| Navigate folders, switch layouts, Back/Forward, then act | Full browser/router journey verifies breadcrumb, tree, rows, selection and exact Cook Asset/Folder arguments at every history position; obsolete lookup and late-catalog cases pass. |
| Built-ins and cooked companions | Shared engine catalog, verified origin grouping and duplicate Default regression; user confirmed the corrected browser. [Typed assets](ED-M07B-typed-assets.md) includes all eleven native built-ins in the combined run. |
| Cylinder and complete generator set cook/load | Engine-owned recipes, source identities and native factory/catalog comparisons; user confirmed both native suites and Cylinder cooking. Packaged preview/Save-reopen covers all eleven choices. |
| Compact single/multi-component filtering | [Scaled layouts](ED-M07B-scaled-layouts.md): component filtering, All reset, original property rows, unchanged dirty/history state and reachable controls. User confirmed the compact selector. |
| Uncooked, missing, stale and cooked-only assets | Shared status reader/provider, read-only inspection, native missing-asset tests and [cooked-only typed use](ED-M07B-typed-assets.md). Selection, Undo/Redo and persisted typed URIs pass. |
| Save a shared material | [Workspace publication](ED-M07B-workspace-publication.md): two current native consumers update through automatic Save and all explicit scopes, preserving scene identity/history. |
| Assign uncooked content into a dirty scene | User confirmed paused automatic cooking, assignment, resume and live update without saving the scene. Native automatic publication and demand-service tests preserve dirty consumers outside the input closure. |
| Repeated saves, newer unsaved edits, pause/cancel/retry | Pipeline `ChangedSavesCoalesceBehindActiveWork`, `LaterDirtyDocumentDoesNotBlockCapturedCookOrBecomeClean`, shared-observer cancellation and pause tests; native cancellation/retry keeps prior output. |
| Undo assignment or switch/close during cooking | Native queued Undo/clear/remove cases, scene replacement, [document transitions](ED-M07B-document-transitions.md) and [project closure](ED-M07B-project-closure.md) reject obsolete ownership. |
| Repeat all four cook scopes, then change a dependency | Native no-op cases preserve content revision and published timestamps; pipeline dependency tests prove selective invalidation/reuse. [Feedback timing](ED-M07B-cook-feedback.md) covers changed and unchanged requests. |
| Save listed & Cook with a save conflict | [Save-conflict recovery](ED-M07B-save-conflict-recovery.md): real atomic material save, blocked original operation, explicit reload, same-operation resume and unrelated dirty documents preserved. |
| Worker/validation/publication/mount failure and cancellation | Actual worker/descendant termination and stream drain; journal boundary, failed mount/rollback and persisted recovery cases. [Import retry](ED-M07B-import-dialog-journeys.md) uses controlled failure after real retention and actual native retry. |
| Reopen, external changes and missing derived output | Persisted provenance and recovery tests; `ImportedSourceChangesRequireExplicitReimport`, `CookRepairsCorruptPublicationMetadata`, missing-output regeneration and [project reopening](ED-M07B-project-closure.md). |
| Offline or mismatched native state | Capability-specific preflight, source-save independence, last-known built-in catalog and repaired-SDK tests; existing Runtime/Managed.Core compatibility coverage and current matched SDK/Interop builds. No separate qualification probe is required. |
| Keyboard, assistive technology, themes and scale | [Scaled controls](ED-M07B-scaled-layouts.md): actual Tab input, keyboard focus, named controls and both themes at 100/150/200%. [Cooking automation](ED-M07B-cooking-accessibility.md) verifies actual peer names and status updates. Standard WinUI semantic resources supply status colours. |

## Cooking acceptance and original review

The [Main repair](ED-M07B-main-repair.md) combines the original -1 failure,
retained prior publication, Go to property, rejected negative edit, repair to
100, unsaved Retry, named save, current status and native/source reopening.
The failed run retains its captured -1.

The additional Cooking gates are covered by the coordinator/publisher suites
and rendered panel cases: all scopes, independent failures/dependent skips,
warnings, first failure, cancellation, automatic visibility, required attention,
removed targets, no-op, offline/recovery outcomes and compact scalable layout.
[Reading-state tests](ED-M07B-cooking-reading.md) additionally prove tail-follow,
scroll-up preservation, per-run position/selection, hide/reopen, source-isolated
buffering and continued updates. Global log traffic never enters these retained
per-run transcripts.

The twelve findings in the [initial UI review](ED-M07B-ux-review.md) map to the
shared status/picker and material-header changes (1, 2, 6, 8), on-demand inspection
and source/output details (3, 4, 11), functional query controls and consistent
navigation (5, 9, 10), complete built-in identity handling (7), and the reviewed
import/collision flow (12). Their corrected controls and native paths are covered
by the records above; user feedback confirmed the browser features, duplicate
removal, native preview updates and startup/shutdown behavior.

## Verification inventory

- ContentPipeline: **449/449**, `artifacts/TestResults/m07b-closeout-pipeline.trx`.
- Runtime: **95/95**, `artifacts/TestResults/m07b-context-runtime.trx`; material
  availability/document suite **56/56** in
  `artifacts/TestResults/m07b-runtime-availability-material-final/abdes_GIGA_2026-09-13_14_50_46_net9.0.trx`.
- Final Cooking/reading/Main/import/recovery/priority group: **32/32**,
  `artifacts/TestResults/m07b-cooking-reading-qualified.trx`.
- Imported/built-in/mixed-selection/native publication group: **34/34**,
  `artifacts/TestResults/m07b-imported-native-use-final.trx`.
- Browser navigation/query/mount controls: **36/36**; Content Browser **134/134**,
  `m07b-browser-history-qualified.trx` and `m07b-browser-history-unit.trx`.
- Import/browser/material scale group **33/33**; inspector/Cooking scale group
  **18/18**; isolated cook feedback **4/4**; document transitions **2/2**.
- Native numeric import: **23/23 per configuration**; expanded portable cases
  validate 16 source profiles and both source copies. Both Debug/Release SDKs
  are installed; redirected-output checks pass without escapes or clipping.

The executable changes and tests were checked for analyzer/IDE diagnostics,
including recommendations. The final cleanup uses the current async lease APIs
and preserves synchronous-conflict assertions. A comment-only correction in
`CommitGroupController.cs` does not change its older implementation.

Commits `4741d0efc` and `f4f234bd1` contain `Fixes #8` and `Fixes #11` respectively.
GitHub still lists both issues open pending integration; this audit records the
implemented fixes and their evidence, not a remote issue closure.

Standalone rendered parity remains ED-M08. The separate ED-M02 viewport gate and
ED-M10 release/GPU qualification are not claimed by this milestone.
