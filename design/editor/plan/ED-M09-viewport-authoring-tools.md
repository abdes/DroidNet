# ED-M09 - Viewport Authoring Tools

Status: `planned; implementation and validation pending`

## 1. Purpose

Deliver the decided single-viewport authoring interaction contract after ED-M08,
using existing camera navigation and ED-M07A's commands/sessions/diagnostics.

## 2. PRD Traceability

REQ-005/006/008/025/027-035/037/041; SUCCESS-003/005/008.

## 3. Required LLDs

[viewport-and-tools.md](../lld/viewport-and-tools.md) section 16,
[property-pipeline.md](../lld/property-pipeline.md),
[documents-and-commands.md](../lld/documents-and-commands.md),
[scene-explorer.md](../lld/scene-explorer.md),
[runtime-integration.md](../lld/runtime-integration.md).

## 4. Scope

Existing Turntable/Trackball/Fly modes, safe focus/capture, Frame Selected/All,
picking, shared selection feedback, World/Local translate/rotate/scale, active-
node pivot, fixed snap increments, camera/light icons and bounded overlays.

## 5. Non-Scope

Multi-viewport stability, alternate validation models, asset editing from gizmos,
advanced manipulators, and navigation that changes authored camera state.
Existing unqualified multi-pane controls are disabled with a reason.

## 6. Implementation Sequence

1. Preserve existing native navigation bindings and implement focus-loss/capture
   release through the runtime input boundary. Add frame selected/all using
   finite world bounds, specified margin and empty/missing-content behavior.
2. Add engine-owned pick requests/results with document/view lifetimes and authored
   node identity. Wire click/Ctrl-click/empty-clear to the shared selection service;
   gizmo/navigation capture takes precedence and stale results are ignored.
3. Implement translate/rotate/scale in World/Local space around the active-node
   pivot, including selected targets under transformed parents. Use the existing
   property session for preview, one-entry commit and exact cancel restoration.
   Reject transforms not representable by the authored model without partial edits.
4. Add the specified snapping and visibility controls, selection highlight and
   camera/light icons. They are view state, not cooked geometry or scene changes.
5. Consume ED-M07A's field/tool diagnostic replacement rules; a gizmo must not
   introduce a separately scheduled full-scene validator or direct interop from VM.

## 7. Project/File Touch Points

WorldEditor SceneEditor Viewport/ViewportViewModel, Inspector/SceneExplorer
selection consumers, existing command/property sessions; Runtime input/picking
contracts; Interop EditorModule navigation and engine-owned picking/render
capabilities; existing editor UI controls/icons and appropriate test projects.

## 8. Risks

Input capture conflicting with field editing, stale picks, nonuniform parent
transforms, multi-target undo and transient overlay state are exercised directly.
A visually moving gizmo is not proof that saved/runtime state changed correctly.

## 9. Validation Gates

- [ ] All navigation/focus/publication-pause cases leave no stuck input/capture.
- [ ] Frame selected/all passes geometry, transformed hierarchy, icon-only,
  missing-bounds and empty-scene cases without authored-camera mutation.
- [ ] Viewport, tree and inspector agree on selection across stale results/deletion.
- [ ] All tools/spaces/snapping/multi-selection pass commit/cancel/undo/redo and
  invalid-transform cases; saved/cooked state agrees with authored results.
- [ ] Overlay controls do not dirty/persist runtime helpers into scenes and
  disabling overlays preserves the ED-M08 comparison conditions.
- [ ] User interaction validation passes on the PRD's qualified small project.

## 10. Status Ledger Hook

Record one ED-M09 validation row with actual interaction evidence. No M04 task
is reopened; ED-M07A supplies shared mechanics and this milestone owns tool wiring.
