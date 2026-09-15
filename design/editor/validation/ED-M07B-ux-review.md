# ED-M07B Content Workflow Review

Date: 2026-09-11. Scope: planning review of the running Debug editor and its
content workflow source. M07B implementation and qualification remain pending.

## 1. Result

M07B needs explicit browser, picker, and material-editor work as well as pipeline
safety. The current editor exposes the relevant entry points, but status
contradictions, navigation mismatches, and unexplained generated assets make it
hard to know what is saved, cooked, usable, or selected for an operation.

The revised [milestone plan](../plan/ED-M07B-safe-content-publication-and-compatibility.md)
assigns these fixes to 07B.5a-f. The
[workflow LLD](../lld/content-cooking-workflows.md) specifies before/after states,
actions, incremental execution, recovery, and proposed automatic triggers.

## 2. Reviewed Journey

The user supplied the open editor with the Vortex project. Inspection covered
navigation, selection, menus, Inspect Cooked Output, pickers, opening an existing
material, and the import file picker, which was cancelled without choosing a file.
No source edits, saves, imports, cooks, or asset assignments were performed.
Screenshots and UI Automation snapshots are local evidence under
`artifacts/m07b-ux-review/`; the links below open the accepted captures.

| Step | Interaction and health | Finding and destination |
| --- | --- | --- |
| 1 | Browse authored scenes: navigation works, state unclear. | Main shows DESC STALE; NewScene2 shows DESC COOK. Neither explains current viewport use or the stale cause. 07B.5a/b. |
| 2 | Browse/select authored materials: readable names, missing next action. | Selecting M_ShinyRed reveals no details or source/output relationship. All material glyphs look like small empty boxes. 07B.5b/e. |
| 3 | Open Cook menu and run Inspect: entry points work, result incomplete. | Inspect returns a green count/path message for the Content root, without an asset/dependency report or freshness/readiness distinction. 07B.5d/e. |
| 4 | Browse cooked materials/geometry: assets discoverable, origin unclear. | Authored and Cooked folders show the same merged rows without read-only/source explanation; technical generated names appear as ordinary content. 07B.5b/c/7. |
| 5 | Open browser options and choose Filter: incomplete. | Menu closes without filter controls or changed results; source confirms no command binding. 07B.5b. |
| 6 | Open material picker: groups useful, state missing. | None, Engine Default, and Content are separated, but Content also contains OxygenEditor_Default. No cook/freshness/readiness is shown for these choices. 07B.5a/c/7. |
| 7 | Open geometry picker: eight built-ins discoverable, duplicate-looking output choices. | Cube/Plane/Sphere coexist with Engine_Generated_BasicShapes_* under Content, without explaining their relationship. 07B.5c/7. |
| 8 | Open M_ShinyRed: document opens, status and layout defective. | Browser says DESC COOK; document says Cook: NotCooked. Cook is clipped at the right edge of the active dock. 07B.5a/e. |
| 9 | Browse empty SourceMedia/DCC: empty state incomplete. | A blank asset pane provides no explanation or first-import guidance. 07B.5b/d. |
| 10 | Switch Materials from List to Tiles after browsing Geometry: incorrect scope. | Geometry tiles appear under the Materials breadcrumb and remain after settling. Returning to List restores material rows; later navigation can again leave rows inconsistent with the breadcrumb. 07B.5b. |
| 11 | Select/right-click/open cooked-only OxygenEditor_Default: no useful outcome. | Selection highlights, but no visible context/details/source-unavailable explanation appears. 07B.5b/c. |
| 12 | Open Import: standard picker works, scope guidance incomplete. | Picker accepts All files (*); no supported-format or destination guidance precedes it. Cancel succeeds. Collision/unsupported handling needs source-backed work and later workflow tests. 07B.4/5d. |

### 1. Authored Scene State

![Authored scenes showing abbreviated cooked/stale badges](../../../artifacts/m07b-ux-review/01-workspace.png)

### 2. Authored Material Selection

![Selected authored material without an asset details surface](../../../artifacts/m07b-ux-review/03-material-selected.png)

### 3. Cook And Inspect

![Existing Cook menu and scoped commands](../../../artifacts/m07b-ux-review/04-cook-menu.png)

![Inspect result exposes counts and a physical output path](../../../artifacts/m07b-ux-review/05-inspect-result.png)

### 4. Cooked Content

![Cooked material folder repeats the merged asset rows](../../../artifacts/m07b-ux-review/06-cooked-materials.png)

![Cooked geometry uses technical generated names](../../../artifacts/m07b-ux-review/12-cooked-geometry.png)

### 5. Filter Entry

![Filter is exposed in the browser options](../../../artifacts/m07b-ux-review/07-browser-options.png)

![Choosing Filter returns to unchanged browser content](../../../artifacts/m07b-ux-review/08-filter-action.png)

### 6. Material Picker

![Material picker has useful groups but no freshness and unclear default variants](../../../artifacts/m07b-ux-review/09-material-picker.png)

### 7. Geometry Picker

![Built-in and generated cooked shapes appear as separate choices](../../../artifacts/m07b-ux-review/10-geometry-picker.png)

### 8. Material Document

![Material document contradicts browser cook state and clips its Cook action](../../../artifacts/m07b-ux-review/14-material-document.png)

### 9. Empty Source Folder

![Empty source media folder provides no next-step guidance](../../../artifacts/m07b-ux-review/15-source-media-empty.png)

### 10. List And Tile Scope

![Materials breadcrumb with geometry rows after switching to Tiles](../../../artifacts/m07b-ux-review/18-tiles-settled.png)

### 11. Cooked-Only Open

![Opening a cooked-only default provides no visible details or source explanation](../../../artifacts/m07b-ux-review/22-cooked-only-open.png)

### 12. Import Picker

![Import opens an unrestricted All files picker](../../../artifacts/m07b-ux-review/23-import-picker.png)

## 3. Priority And Scope

1. **Correctness:** repair navigation/query scope and share authoritative state
   across browser, material editor, pickers, and runtime. A wrong row under a
   folder breadcrumb can make an operation target surprising content. Source
   timestamps and a document's initial NotCooked value cannot establish truth.
2. **Discovery and use:** keep one logical identity, explain built-in versus
   authored versus derived origin, show uncooked/stale choices, and make details
   and applicable next actions available. Group companions using proven provenance,
   without changing saved references or conflating independent assets.
3. **Flow completion:** finish filters, empty states, import destination/collision
   handling, dirty-input save/retry, scoped progress/cancellation, and meaningful
   Inspect/Validate. Existing native transaction safety remains mandatory.
4. **Usability and accessibility:** keep commands visible at dock widths, replace
   inappropriate/missing type glyphs, give controls meaningful accessible names,
   and preserve keyboard focus through async updates.

The source review also found `File.Copy(..., overwrite: true)` in browser import
and early failures written only to Debug. These are source findings, not a tested
overwrite incident. `AssetIdentityReducer` compares timestamps and emits a missing
cooked diagnostic for an otherwise valid uncooked descriptor. These join the
visible findings in the revised plan.

## 4. Accessibility Evidence And Remaining Validation

UI Automation exposes some toolbar buttons with empty names, browser rows as
the complete `ContentBrowserAssetItem` record string, breadcrumb buttons as
`BreadcrumbEntry { ... }`, and splitter names as unresolved resource URIs.
M07B must give its browser/cook/picker controls concise names that match the UI.
This is accessibility-tree evidence; full screen-reader behavior and keyboard
completion still require the implementation qualification journeys.

The observed display is 3840 pixels wide with the user's existing dock layout.
The material action clipping is present in that layout. Other DPI settings,
themes, narrowed docks, and the PRD's 1,000-entry performance workload were not
exercised in this planning review and remain explicit 07B.5f checks.

Before-first-cook, active cook/progress, cancellation/failure recovery, collision,
and multi-output import are covered by source review and the revised state/action
matrix; they were not simulated by mutating this project. Their acceptance tests
must exercise real workflows in the M07B implementation. This review does not
claim M07B validation or reopen M07A.

## 5. Product Decision

The recommended hybrid policy automatically cooks changed saved content after
Import/Save and supplies missing/stale content on assignment or active-scene
demand. It leaves browsing, unsaved edits, project-wide cooking, and external
reimport distinct, with a session Pause automatic cooking control. Saving shared
material content can consequently refresh every scene use after publication.
The complete trigger and recovery tables are workflow LLD sections 4-6.

The user directed implementation of revised M07B on 2026-09-11. D1 is accepted
and the PRD/material/pipeline clauses are reconciled before implementation.

## 6. Follow-Up Reports And Source Review

The user reported a project-cook failure after choosing Cylinder:

> Cook failed for the active project. Scene node `New Entity 5` references unsupported geometry `asset://Engine/Generated/BasicShapes/Cylinder`.

The described edit was to New Entity 6. `ProceduralGeometryDescriptorService`
recognizes only Cube/Sphere/Plane and skips Cylinder; `SceneDescriptorGenerator`
then emits the reported unsupported-geometry error. This proves a coverage gap,
not that either node name in the report is wrong. The regression must identify
the captured saved node ID/revision and keep diagnostic navigation accurate.

The M07B.7 baseline covered eleven exposed generator names, including duplicate
sphere naming, through Save -> Cook Current Scene / Cook Project. ED-M08's
canonical palette supersedes that inventory: ten authoring choices, one
IcoSphere identity, and separate internal tool resources. Cylinder/Cone factory
and importer segment defaults now share the native recipe authority.

The user also reported that the node inspector's component list stretches,
component selection does not filter editors, and single/multi-node layouts waste
vertical space. Source confirms `TopPaneHeight = 2*`, `PropertyPaneHeight = 3*`,
and a hidden component list in multi-node mode. The selection handler only sets
the details view-model's deletion target; the host editor filter ignores it.

M07B.5g and property-inspector section 9.1 now require content-sized component
selection, a compact multi-node summary, component-only filtering, and deselect/
All to restore every applicable editor. Filter changes remain view state and
preserve gesture/history/validation behavior. The follow-up editor process was
closed; these additions use the user report and current source, with single/
multi-node interaction and sizing checks assigned to implementation validation.
