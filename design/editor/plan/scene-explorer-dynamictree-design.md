# Scene Explorer and generic DynamicTree design implementation

Status: `in_progress` — SE-01 validated; SE-02 landed (awaiting review); SE-03
planned.

| Outcome                                                                                              | Remaining                                                                | Evidence                                                                                                                                                                                                                   |
| ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SE-01 generic tree capabilities and the single-scene DemoApp showcase are implemented and validated. | User demo acceptance; SE-02/03 editor authoring and runtime integration. | Final source review; 151 model and 89 UI tests passed; scoped builds and changed-file hooks passed. Live demo checks cover native compact/comfortable filtering, aligned hover actions, loaded status and copy/paste/undo. |

## 1. Goal, scope and design authority

Implement the Scene Explorer shown at <http://127.0.0.1:5178/> using WinUI 3 and
the existing **generic** DroidNet DynamicTree. Deliver its appearance,
discovery, selection, protection and hierarchy operations as real editor
workflows, not web-demo behavior or decorative controls.

**Do not capture, freeze or compare against a baseline of the existing Explorer
UI.** Its red border, random scene thumbnail, dialog rename and incomplete
command wiring are not the target. Inspect existing code/tests only to
understand APIs, domain invariants and reusable mechanisms. Add tests against
the intended design. Do not create a separate audit/baseline slice.

Read the showcase's **Design brief → Scene Explorer**, **Property
dependencies**, **WinUI & accessibility** and **DroidNet control map**, then
exercise the workspace. Use the interactive showcase to understand affordances
and state transitions; use the brief for their meaning. Browser pixel values are
not literal WinUI settings: use DIPs, semantic brushes and the brief's
typography/layout roles.

Other required references:

- [Oxygen engineering rules](../../oxygen/RULES.md) and repository AGENTS.md.
- [Scene Explorer LLD](../lld/scene-explorer.md), [scene authoring
  model](../lld/scene-authoring-model.md), [documents and
  commands](../lld/documents-and-commands.md).
- [Settings architecture](../lld/settings-architecture.md), particularly the
  workspace-visibility storage/lifetime contract.
- [Property pipeline](../lld/property-pipeline.md) and [viewport interaction
  contract](../lld/viewport-and-tools.md#16-v01-viewport-interaction-contract).
- [DynamicTree
  filtering](../../../projects/Controls/DynamicTree/design/filtering.md),
  [mutation
  UI](../../../projects/Controls/DynamicTree/design/tree-mutation-ui.md),
  [typeahead/focus](../../../projects/Controls/DynamicTree/design/typeahead-focus.md),
  and its actual public APIs/tests. The README contains examples that do not
  match every current API; source is authoritative for what already exists.
- [Build/test workflows](../../../tooling/doc/build.md) and [MSTest
  guidance](../../../.github/prompts/csharp-mstest.prompt.md).

This plan is scoped design implementation, not a reopening of delivered ED-M03
or a competing ED-M09 viewport milestone. It owns Explorer/control work and
consumes the runtime picking/overlay capabilities owned by ED-M09 and the
existing runtime integration boundaries. Missing native support must be
implemented by its owner under explicit authorization, not hidden behind a
successful UI-only test.

Requirements: REQ-004/006/007/008/022/024 for hierarchy authoring; the shared
selection, workspace visibility and picking contracts of the linked LLDs. No new
physics, volume, docking, gizmo system, component inspectors or global command
palette is introduced here.

### Research-informed decisions

The interaction rules below are recommendations for Oxygen, not claims that the
showcase demonstrated every edge case. They incorporate the user's requirement:
**one loaded scene at a time, the project's configured default on activation,
and a different scene only after an explicit load.** They remain within the
three slices. Review changes to accepted LLD/model contracts once before
implementation.

Sources consulted on 2026-10-03:

- [Unity 6.6
  Hierarchy](https://docs.unity3d.com/6000.6/Documentation/Manual/Hierarchy.html)
  explicitly separates sibling reordering from parenting and distinguishes Paste
  as Child with local/world transform preservation. This supports explicit
  intent rather than a surprising implicit parent change. Oxygen chooses
  preserve-world for its normal reparent operations; it does not copy Unity's
  multi-scene model.
- [Godot
  Node.reparent](https://docs.godotengine.org/en/stable/classes/class_node.html#class-node-method-reparent)
  defaults to retaining global transforms and documents recursive duplication.
  This supports preserving pose and complete subtrees; Oxygen retains its own
  identity, reference and transform rules, including allowed duplicate node
  names.
- [Microsoft
  drag/drop](https://learn.microsoft.com/en-us/windows/apps/develop/data/drag-and-drop)
  separates source allowed operations, target acceptance, asynchronous deferrals
  and completion. Oxygen's domain command remains the one mutation authority;
  DropCompleted must not delete the source a second time.
- [Microsoft
  clipboard](https://learn.microsoft.com/en-us/windows/apps/develop/communication/copy-and-paste)
  distinguishes Copy/Move payloads and emphasizes built-in text-control
  handling. Oxygen's node clipboard must not steal a rename/search TextBox's
  native shortcuts.
- [Microsoft contextual
  commanding](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/collection-commanding)
  recommends item context menus alongside frequent-action accelerators,
  including right-click, Shift+F10/Menu key and touch/pen invocation. The
  toolbar is the discoverable fast path; the context menu gives the same
  commands near the target. Unity's Hierarchy documentation also exposes
  explicit child-paste and sibling operations through right-click. Oxygen should
  adopt that clarity, not copy every peer editor command or replace its existing
  toolbar with a hidden menu.

Peer editors inform predictable interaction, not Oxygen's serialization
contracts. No research source justifies adding multi-scene loading, arbitrary
cross-project asset transfer, destructive folder deletion or a second
clipboard/command owner.

## 2. Findings that determine the work

Paths in this table are repository-relative. These are source facts, not a
preservation checklist for the old UI.

| Current implementation                                                                                                                                                                                                                               | Required response                                                                                                                                                                                                                                           |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `WorldEditor/src/SceneExplorer/SceneExplorerView.xaml` has a literal red border, random scene thumbnail, no search or picking row, and a non-compact bottom toolbar with overflow disabled.                                                          | Replace its presentation with the showcase structure and deliberate semantic icons; compose command surfaces through DroidNet ToolBar.                                                                                                                      |
| `SceneExplorerView.xaml.cs` uses reflection to find commands/methods and opens a rename dialog.                                                                                                                                                      | Use typed commands and the tree's inline rename path; remove reflection/fallback logging and duplicate rename UX.                                                                                                                                           |
| `SceneExplorerViewModel` owns loading, adapter indexing, selection publication, clipboard, mutation events, history and dirty state in one large class.                                                                                              | Keep it a UI orchestrator; put projection, selection bridging and authoring transactions on focused owning collaborators.                                                                                                                                   |
| Tree-first mutation records undo and performs backend work later, sometimes fire-and-forget. `SceneNodeAdapter.Label` writes the domain object directly; the VM observes it afterward.                                                               | Execute a validated document command first, then reconcile the projection. Rename needs a generic async commit hook, not an after-the-fact label observer.                                                                                                  |
| Explorer Cut calls Copy then Delete. Its clipboard differs from DynamicTree's clipboard, and `SceneNodeAdapter.CloneSelf()` retains the original SceneNode.                                                                                          | Unify keyboard/menu/toolbar intent; stage cut without deletion and deep-copy authored data on copy/paste. Never duplicate an adapter around the same node as a copy.                                                                                        |
| DynamicTree already has lazy adapters, ShownItems/FilteredItems, selection, typeahead, focus, clipboard, move events, row recycling and 25%/middle/25% drop hit testing.                                                                             | Reuse these; do not implement another tree, selection algorithm, filter engine or clipboard inside the view.                                                                                                                                                |
| DynamicTreeItem has a fixed 30-DIP template row, no reusable trailing-content slot, and direct Label assignment on rename. Into hit testing exists but its indicator maps to None. Before/After move hit testing restricts items to the same parent. | Extend generic row presentation and request/commit seams; show Into, validate the destination independently of source parent, and make measurement text-scale-safe.                                                                                         |
| Existing filtering does loaded-subtree matching without loading children, then renders a subset of ShownItems. Selection indices remain in ShownItems; tests explicitly require default SelectAll to include unfiltered shown items.                 | Do not silently change generic semantics. Add an explicit displayed-interaction policy for consumers needing visible-range selection; Explorer opts in. Scene search expands match paths using its domain index, not by force-loading arbitrary lazy trees. |
| ISceneSelectionService stores node IDs but exposes only node lists, no change notification or public primary identity. Root/folder/mixed rows disappear when the VM publishes only nodes.                                                            | Extend the existing owner with typed Explorer selection context and primary identity; bridge Inspector/viewport without pretending folders are nodes or an empty node list always means scene selection.                                                    |
| SceneExplorerService currently reparents a node to a folder's nearest scene-parent scope. SceneMutator changes Parent without compensating local transforms.                                                                                         | Folder grouping must never implicitly reparent; true reparent uses preserve-world-pose semantics and validates representability before mutation.                                                                                                            |
| The reviewed WorldEditor/Runtime managed surfaces do not expose a complete picking-policy or editor-hide service. Settings LLD specifies `WorldEditor/SceneVisibility`, but no matching implementation was found.                                    | Implement workspace state at its editor owner and verify supported runtime view APIs. Do not write authored visibility flags as an eye-button shortcut.                                                                                                     |

### Project data model and activation gap

Read `projects/Oxygen.Editor.World/src/IProject.cs` and `IProjectInfo.cs`,
`projects/Oxygen.Editor.Projects/src/{Project,ProjectInfo,ProjectContext,ProjectManagerService}.cs`,
and `WorldEditor/src/Workspace/WorkspaceViewModel.ResolveInitialSceneAsync`.

- `Project.Scenes` holds scene entries discovered from storage. Discovery clears
  their RootNodes; being listed is not being loaded for editing.
- `Project.ActiveScene` is an in-memory selection with a first-scene fallback.
  That fallback depends on discovery order and is not a configured project
  default.
- `ProjectInfo`/`IProjectInfo` and the serialized ProjectManifestDescriptor
  currently have **no persisted default-scene identity**.
  InitialSceneAssetUri/OpenInitialScene are activation context, not manifest
  configuration. Template StarterScene is returned from project creation but is
  not written as a project default.
- Workspace startup currently considers an activation URI, then user-local
  LastOpenedScene, then ActiveScene. This does not implement the requested
  policy.
- DocumentManager opens/selects scene documents and sets ActiveScene before the
  Explorer proves the scene loaded. Explorer also sets it from document events.
  These are competing, premature activation writes, not the target architecture.
- SceneDocumentMetadata is user-nonclosable. Do not flip IsClosable or bypass
  dirty guards to make scene replacement work; coordinate replacement at the
  existing document/project lifetime owner.

**Required owner-level extension:** recommend `DefaultSceneId : Guid?` in the
project manifest/model, validated against that project's scene entries and
propagated through ProjectContext. Stable scene ID survives rename; the existing
asset URI resolver can resolve explicit asset-open requests to that ID. It must
not become a second persistent default field. Update ToJson's private manifest
descriptor, load/validation, metadata-copy paths, template creation and tests
together. Approve the schema/version handling in Projects; do not invent a local
Explorer JSON key or silently treat legacy discovery order as configuration.

For projects with scenes but no configured default, require an explicit choice
and offer to configure it. For a missing/invalid configured default, show a
recoverable load/configuration error and allow explicit Open Scene; do not load
the first or last-opened scene silently. A genuinely scene-less project can show
the create/open empty state. Existing projects need this configuration handling;
changing the project's default is project metadata, not scene undo/dirty state.

`WorldEditor` above means `projects/Oxygen.Editor.WorldEditor`; `DynamicTree`
means `projects/Controls/DynamicTree`. Recheck the source when implementing; do
not reintroduce already completed inspector work.

## 3. Non-negotiable generic-control boundary

DynamicTree must remain usable by Scene Explorer, Controls DemoApp and unrelated
consumers. It must have **no Oxygen references or concepts**.

| Generic DynamicTree owns                                                                                                         | Oxygen owns                                                                                                          |
| -------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Indentation, disclosure, label/rename editor, optional content templates/slots, focus/selection/cut/drop visuals.                | Scene/root/folder/node distinctions, component-derived icons, root count, eye and lock button templates.             |
| Interaction over ITreeItem; generic mutation/rename requests and completion; container preparation/clearing and input isolation. | Node/folder IDs, document lifetime, graph/layout validation, deep duplication, command/history/revision/sync policy. |
| Predicate projection and explicit interaction-scope policy; generic lazy loading and item realization.                           | Name search, full-scene domain index, match-path expansion/restoration, result count, Inspect/reveal.                |
| Generic operation eligibility, including existing ICanBeLocked semantics. Lock does not forbid inspection/selection.             | Local/inherited editor hide/lock, actual scene-parent evaluation, persistence and viewport picking categories.       |
| Style resources, measured sizes and reusable automation behavior.                                                                | Oxygen visual tokens, icon meanings, tooltips and cross-panel summaries.                                             |

Do not add Mesh/Light/Camera enums, IsEditorHidden, scene serialization, native
handles, document services or eye/lock commands to DynamicTree/ITreeItem. Add
generic **content/template slots**, not a hardcoded eye and padlock. Adapters
can expose feature state to supplied templates without the library interpreting
it.

Extend natural owning APIs. Retain the existing default behavior for generic
consumers unless a deliberately reviewed correction is necessary. Empty
extension slots must reserve no width by default. Oxygen may supply a consistent
reserved trailing width to align actions; do not indent that column with the
label.

Recycled rows must detach subscriptions, cancel old rename/drop work and clear
transient visual state. A row must work with an ITreeItem implementation that is
not TreeItemAdapter: correct concrete casts in modified paths rather than
exposing ITreeItem APIs that still require the concrete type internally.

## 4. Target Explorer behavior

### Single loaded scene and switch lifecycle

1. On fresh project activation load its configured default. An explicit
   Open/Load Scene request selects another scene for the current activation
   only; it does not rewrite the default. LastOpenedScene may remain usage
   history, but must not override startup. Template creation should seed the
   manifest's default from StarterScene. A repeated ProjectContext notification
   for a mount/settings change is not fresh activation and must not reset an
   explicit loaded scene.
2. Explorer root, Inspector scene context, editing viewport, hierarchy commands
   and Project.ActiveScene follow the **same accepted loaded-scene identity**.
   Focusing a material/inspection tab does not unload or reset it. Explicitly
   loading another scene is a coordinated replacement, not another Explorer
   root. No multi-scene drag destinations or retained inactive editable graphs.
3. Before replacement settle active edits, then use existing Save/Discard/Cancel
   guards if unloading loses unsaved authoring. Cancel or save failure leaves
   the old scene, selection and clipboard intact. Do not silently auto-save,
   discard, or load a new scene merely because a document opened in the
   background.
4. Stage source/DTO validation before replacing the current scene; metadata and
   serialized staging payloads are not additional loaded editing scenes. During
   handoff disable authoring/drop, cancel gestures, retire old adapter/runtime
   targets and clear old scene UI before installing the new graph. Publish the
   accepted identity once installation succeeds. Do not show old rows under a
   new scene title, assign ActiveScene before success, or route work to half a
   load.
5. If a same-project replacement fails before retirement, retain the old
   accepted scene. If failure occurs after retirement, report unavailable state
   and offer reload of the previous saved scene; never present a stale graph as
   current. Project-switch failure must not expose the previous project's rows
   as the new project's scene. New project activation again follows its own
   configured default.
6. Requests carry project activation, scene/document lifetime and operation
   identity; asynchronous completion checks all three. Latest accepted request
   wins. A cancelled or older load cannot overwrite a later one. Clipboard cut,
   drag, rename, timers and native picks cannot span a committed replacement.

This lifecycle belongs to Projects/document/workspace integration, not
DynamicTree. Bind the Explorer to the current project context; do not retain a
constructor- captured IProject across project switches. Keep metadata/history
owned by their existing services without keeping a second live scene tree just
for a transfer.

### Presentation and discovery

- A deliberate Scene Explorer heading/icon consistent with the Inspector; no
  docking-layout redesign. Where the native dock tab already owns the title,
  avoid a duplicate heading in the content body. Keep the native dock/tab
  ownership.
- Search field with “Find an object…” intent, clear action and a match/result
  count. Search matches names, includes group/scene-parent context and opens
  match paths. Empty scene and no matches are different states. Search does not
  alter selection, authored data or viewport eligibility. Clearing restores
  previous expansion.
- Separate **Viewport picking** category toggles: Mesh, Light, Camera, only when
  represented in the scene. Determine categories from the whole scene, not
  search results. No unsupported Physics/Volume buttons. Disabling a category
  does not hide its rows/geometry or block selection from the Explorer.
- Stable row columns: indentation/disclosure/type icon/name, optional root
  count, reserved eye and lock actions. Essential state actions are always
  discoverable, not hover-only. Use semantic node-type icons, not random
  previews; a node with children does not become a folder icon.
- Target 32-DIP compact and 40-DIP comfortable minimum interaction rows, 14-DIP
  body/input and 12-DIP caption roles, 4-DIP spacing rhythm, 16-DIP action
  glyphs and 20-DIP section glyphs. Implement through scoped
  resources/MinHeight/padding; text scaling may grow rows. Do not invent a
  global density preference here; consume an existing owner if available and
  test both resource profiles.
- Long tree labels may ellipsize with their full name available through tooltip
  and automation; header/search/error text must not clip. Narrow layout keeps
  all actions reachable. Semantic Light/Dark/contrast resources and visible
  focus are required; selection is not a substitute for focus.
- Bottom commands in showcase order: **New node, New folder, Rename, Cut, Copy,
  Paste, Delete**. Share typed command instances and availability with context
  menus/keyboard. Use tested ToolBar overflow at narrow widths rather than
  hiding commands or forcing a fixed-width strip.

### Required contextual menu: contents and targeting

**Implement a right-click/context-action menu.** It is a complementary command
surface, not a new command system and not a replacement for the context-aware
toolbar or always-visible eye/lock actions. It makes destination-specific Paste
and hierarchy actions discoverable without requiring pointer drag/drop.

Use the existing `DroidNet.Controls.Menus` data/host infrastructure:
IMenuSource, MenuItemData/MenuBuilder and ContextMenu attached
surface/PopupMenuHost. Keep the normal textual menu treatment and short related
groups, with Delete last in its edit group; do not build another icon-only
command bar inside the popup. Microsoft also recommends CommandBarFlyout for
common edit actions, but the already-owned DroidNet menu gives consistent
command identity/input/accessibility across Oxygen without a second menu
renderer. Native TextBox context menus remain untouched.

#### Invocation and immutable context

- Right-click a selected row: preserve the whole selection, including mixed
  node/folder membership. The clicked row is the menu anchor, not a new primary
  selection and not permission to reduce the batch to one item.
- Right-click an unselected row: settle the current Inspector edit through its
  normal policy, select/focus that row exclusively, then open its menu. Lock
  prevents manipulation, not selection/context access. Do not apply Ctrl/Shift
  selection modifiers just because they happen to be held during right-click.
- Right-click tree background: open a scene-root-targeted creation/Paste menu
  **without clearing the existing selection or changing the Inspector**. Exclude
  selection edit commands so background Delete can never delete unrelated
  selected rows. Do not also open the row menu by bubbling the same request
  twice.
- Shift+F10/Menu key or touch/pen context action: invoke the same policy for the
  focused/requested row. With focus on a selected row, retain its batch; focus
  on an unselected row selects it before opening. Keyboard placement is near the
  row, not at stale pointer coordinates. On tree background it uses the root
  context.
- Invoking a menu never begins drag, expands a disclosure, toggles protection or
  dirties a document. When inline rename/search has focus, its TextBox owns the
  context action and Cut/Copy/Paste; no tree-level hijacking. Starting another
  row's context action settles pending rename using the documented commit/cancel
  policy.
- Freeze a typed menu context at opening: project activation, loaded
  scene/document lifetime, selected identities/order, anchor and resolved
  destination. Commands validate this captured context again at execution and
  after asynchronous work. They must not reread current selection and operate on
  a different row. Close the menu when its context is invalidated by selection
  change, target deletion, row recycling, scene replacement or project switch.
  Lock/clipboard changes may update eligibility without changing the captured
  targets or rearranging rows.
- Restore focus to the requesting row on dismissal if it survives; after Delete,
  use the selection owner's surviving neighbor; after Rename, focus the inline
  editor. Escape closes the popup first and does not simultaneously clear Cut or
  scene selection. Keep tree/viewport shortcuts from firing while menu
  navigation consumes them. Root menu with no loaded scene offers no inert
  creation/Paste.

#### Menu contents by context

Maintain group/order consistency within each menu shape. Omit commands that have
no semantic meaning for that kind; retain relevant commands disabled when locks,
clipboard state or validation currently prevent them. Remove empty separators.

| Context                              | Creation group                          | Edit group                                                                                                                      | Secondary groups                                                                                                                                                                                 |
| ------------------------------------ | --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| One node                             | New child node; New folder beside node. | Rename; Cut; Copy; Paste; **Paste as child (keep world pose)**; Delete node.                                                    | Hierarchy: Expand/Collapse when it has children; Remove from folder when directly grouped; Move to scene root when not already directly there. Editor state: Hide/Show in editor; Lock/Unlock.   |
| One folder                           | New node in folder; New subfolder.      | Rename; Cut; Copy; Paste; **Remove folder (keep objects)**.                                                                     | Hierarchy: Expand/Collapse when it has children. No fake Hide/Lock/transform actions. Folder moves use Cut/Paste or drag within its valid scope.                                                 |
| Several nodes, folders or mixed rows | None: choose one creation destination.  | Cut; Copy; Paste when the shared destination resolver is unambiguous; Delete selected items. No multi-rename or Paste as child. | For node-only batches, explicit Hide selected / Show selected and Lock selected / Unlock selected. Omit protection for mixed/folder-only batches rather than silently acting on the node subset. |
| Scene root row                       | New node; New folder.                   | Paste at scene root. No Rename/Cut/Copy/Delete of the scene itself.                                                             | Expand/Collapse; Show all in editor when hidden entries exist. Scene Save/Load/default configuration remain with their project/document command surfaces.                                        |
| Tree background with a loaded scene  | New node; New folder.                   | Paste at scene root.                                                                                                            | Show all in editor when needed. No inherited selection-specific commands.                                                                                                                        |

Creation requires one destination (scene root, node or folder); multiple
selected rows do not invent a creation parent. Labels can explain the context,
but action IDs, icons, eligibility, command ownership and resolved effect must
match the toolbar. “Paste” retains section 4's sibling/group/root resolver;
“Paste as child” explicitly selects its child destination and preserves world
pose. Do not add different implicit local-transform behavior to its keyboard
equivalent.

The Hierarchy group provides a small keyboard-accessible alternative to drag:
**Remove from folder** promotes a node to the enclosing visual container without
changing its actual Parent/TRS; **Move to scene root** uses the same validated
preserve-world move request as a root drop, and can also remove grouping for an
actual root node. Do not silently unwrap descendant folders or bypass scope/lock
checks. Avoid a nested menu containing the entire scene tree, a new
reparent-picker framework, Frame commands with missing viewport capability, or
redundant Inspect commands when selection already displays the Inspector.

For workspace state, show explicit opposite verbs for a definite single-node
state. For mixed node batches show explicit Hide/Show and Lock/Unlock choices,
not a guessed boolean toggle or a misleading checkmark. Eligibility checks the
entire captured selection. An inherited state disables the blocked action with
the named ancestor explanation; it never unlocks/shows parents as a side effect.
Keep disabled reasons accessible via existing help/tooltip/automation
mechanisms, not an unrelated paragraph inside the menu. Folder-removal wording
makes its non-destructive effect clear; node Delete must explain subtree impact
when asking for confirmation, including descendants hidden by search/collapse.

#### Shared implementation, not duplicate state

1. Define the supported actions once in an Explorer-local command presentation
   owner: stable identity, contextual label, icon, real shortcut and eligibility
   reason. Project menu subsets per captured context; toolbar resolves its
   current selection through the same policy. Both invoke the same typed
   authoring or workspace commands. No reflection, menu-specific history or
   direct setters.
2. Current MenuItemData passes **itself** to ICommand.CanExecute/Execute; it has
   no arbitrary CommandParameter property. Use a small context-bound invocation
   adapter to forward the captured ExplorerCommandContext to the common typed
   command if needed. Do not assume WPF CommandParameter binding exists here or
   make the shared command silently use whichever selection is current later.
3. DynamicTree only supplies/reuses generic item/context-request and realization
   hooks; Oxygen constructs the menu. Do not add a SceneExplorer menu,
   scene-kind enum or dependency on Oxygen services to DynamicTree. A non-Oxygen
   consumer must be able to attach a different context menu or none at all.
4. Resolve context/selection before the menu host opens; do not depend on event
   registration order between an attached menu's ContextRequested handler and a
   later selection handler. Use the existing host/request contracts or add a
   minimal generic prepare-context hook at its natural owner if necessary.
   Verify menu reopening after unload/reload and keyboard-versus-pointer origin;
   the current ContextMenu surface disposes its host on Unloaded. Fix a needed
   generic lifecycle/input bug in Menus, not with an Explorer popup workaround.
5. Use the host for popup placement, focus, dismissal, theming and automation.
   No permanent popup per scene node: attach/create presenters for realized rows
   and dispose/detach them on clearing. Do not share one mutable captured
   context across recycled rows. Advertise shortcuts only when registered and
   scoped; AcceleratorText alone is not an accelerator implementation.

### Selection, rename and protection

- Root selects Scene Inspector; folder selects a grouping summary; node selects
  Component Inspector. Folder-only and mixed node/folder selection never display
  an arbitrary node or accidentally select scene environment. Root is exclusive;
  Ctrl membership applies to nodes/folders. Shift range uses the displayed rows.
- One document-scoped selection owner, with ordered identities and explicit
  active/primary identity. Keep node selection for viewport operations separate
  from Explorer row-kind context, not as a second independent authority.
- Clicking eye/lock/disclosure or invoking its keyboard action does not
  accidentally select, rename or start dragging the row. Context-menu invocation
  and row selection follow one deliberate policy. Editing keys belong to inline
  rename before global Delete/Cut/Paste/Undo/typeahead handlers.
- F2/Rename starts the same inline editor. Enter commits once through a document
  command; Escape cancels; invalid names show local feedback. Capture
  item/document identity before awaits. No direct adapter setter followed by
  history observation.
- Eye = **Hide in editor**; lock = **prevent manipulation, allow inspection**.
  Local state is workspace state, not authored visibility or component
  protection. Effective node state follows actual scene ancestry, not a logical
  folder's visual position. Inherited action is disabled with a named
  parent-state explanation; showing/unlocking a parent preserves child local
  choices.
- **Show all** clears current-scene local hide entries, not locks, search or
  picking categories. It is a single recovery action and never changes scene
  history/dirty state. Root/folders have no fake geometry-hide action.
- Enforce effective lock in rename/delete/reparent/component/property
  manipulation and tools, not just the row buttons. A batch containing
  ineligible targets is rejected atomically, with an explanation, rather than
  partially editing a subset. Copy/inspect of locked nodes remains possible;
  paste into locked destinations does not. Existing structural/component locks
  remain distinct.

### Hierarchy and clipboard

- Row top/bottom quarters mean **Before/After**, with an insertion line; middle
  means **Into**, with a visible container target. Hit testing and displayed cue
  must agree. Preserve batch relative order and drop once; selected descendants
  travel with selected ancestors once.
- Folder moves are grouping-only. A drop that cannot be represented within the
  approved layout model must reject with a clear cue/message, never secretly
  reparent nodes. Same-scene node/root drops perform real reparent and preserve
  world pose. Before/After resolves a destination sibling index, not an index
  into a filtered list. Reordering must actually persist the requested order;
  remove unconditional “folders first” sorting where it contradicts authored
  layout.
- Validate graph and layout cycles, root/protected items, locks, stale targets
  and scene ownership before mutation. Do not resolve a stale drag to a
  different active scene merely because an ID matches.
- Compute preserve-world local TRS using the existing transform convention and
  IgnoreParentTransform behavior. Check inverse/decomposition/recomposition;
  reject singular, non-finite or unrepresentable shear results atomically. No
  approximate TRS, new Euler convention or viewport camera movement.
- Cut marks/stages source roots; it does **not** delete immediately. Paste
  performs the move in one authored transaction. Escape/cancel clears cut state.
  Source deletion/reload invalidates cut payloads. Copy snapshots authored
  subtrees/layout, then Paste creates new node/component/folder identities and
  remaps internal references according to domain policy; asset references remain
  shared identities.
- Create/rename/delete/move/paste each use the scene's existing document
  history, revision/dirty and save pipeline. Successful batch = one undo step;
  rejection/no-op = none. Live-sync failure preserves the valid authored
  operation and reports preview divergence; it is not an excuse for half a
  graph/layout transaction.

### Precise drop intent and cancellation

| Destination                                        | Node roots                                                                                                                                                      | Logical folder roots                                                                                                      |
| -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Into scene root, or clearly marked empty root area | Reparent to root, preserve-local by default (D1); append in stable order.                                                                                       | Move grouping to root; contained nodes keep their actual scene parent.                                                    |
| Into node                                          | Reparent to that node, preserve-local by default (D1); append as children.                                                                                      | Accept. A folder move whose scene-parent scope differs performs the lineage-driven reparent inside the same command (D2). |
| Into folder                                        | Group; where the folder's scene-parent scope differs, the same command performs the reparent (D2).                                                              | Nest grouping; reject cycles. Cross-scope grouping reparents — it is not rejected and not a separate confirm step.        |
| Before/After node or folder                        | Insert into the target's underlying visual sibling list. Reparent node roots when that list belongs to another node/root scope, preserve-local by default (D1). | Reorder/regroup; a scope change reparents within the same command (D2).                                                   |

Use the current minimal folder-lineage model deliberately. Superseded 2026-10-04
(D2): grouping a child under a folder in another scope is **not** a two-step
"explicitly reparent first, then group" flow — `MoveNodesToFolderAsync`
validates and performs the lineage-driven reparent inside the same command and
undo step. Reject with a useful reason, not a mystery no-drop cursor, for the
cases that remain genuinely invalid: cycles, stale targets and unrepresentable
transforms. Nodes plus folders in a batch are accepted only when every
destination rule is valid.

- Begin drag from an already selected row without collapsing multi-selection.
  Dragging an unselected row selects that row first. Row actions/rename editor
  are not drag handles. Snapshot identity and the normalized root set at drag
  start.
- Default is Move; Ctrl at the target requests Copy. Show “Reparent”, “Move to
  folder”, “Reorder” or “Copy” with the destination/count, plus matching
  insertion cue. A forbidden operation advertises None and its reason. No Link
  operation.
- Ctrl-drag duplicates through the same snapshot/duplicate command used by Paste
  but **does not overwrite or consume the user's clipboard**. Do not implement
  it by invoking CopyItemsAsync followed by PasteItemsAsync as a side effect.
- Hover Into a valid collapsed destination for the existing 600-ms delay to
  expand; edge-hover scrolls the bounded tree. Auto-expansion/scroll are
  transient view actions, not authored history. Cancel restores drag-induced
  expansion where safe; successful drop leaves its destination open and
  reveals/selects moved roots.
- While searching, retain displayed-range selection and target by stable sibling
  anchor identity, not row index. Before/After insert relative to that actual
  anchor with hidden siblings retaining their order. Do not silently relocate a
  moved root elsewhere merely to keep it matching search; report/reveal its
  result.
- Normalize ancestor/descendant roots before calculating destination indices;
  adjust same-parent insertion after removing source roots; detect exact no-ops.
  Drop revalidates current parents, locks, targets and TRS representability. If
  a changed destination no longer matches the shown intent, reject rather than
  guess.
- Escape, unsupported external payload, outside drop, document/project
  replacement or disposed row cancels with no mutation/history. Stop
  hover/scroll timers and clear cues. Do not replace the whole selection as a
  side effect of cancellation.
- Use WinUI DataPackage allowed/accepted operations and required deferrals
  around awaited work. A successful drop submits exactly one domain command.
  DropCompleted clears gesture state only: the command already moved the source.
  Do not delete it again using the generic file-transfer example from Microsoft.

### Clipboard payload, destinations and switch behavior

One Oxygen Explorer clipboard service owns node/folder transfer for the active
project session; DynamicTree only dispatches generic intents. Its immutable Copy
payload contains source project/scene identity, supported authored
node/component DTOs, logical folder layout, root world poses and reference-remap
metadata. No live adapters, SceneNode pointers, native handles, dirty state or
history records. It copies the full authored hierarchy, including
collapsed/search-hidden children, not just realized rows. Duplicate mutable DTO
collections as well as their records.

| Clipboard event                              | Copy                                                                                                            | Cut                                                                                                                                                     |
| -------------------------------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Capture                                      | Snapshot accepted source values; do not modify/dirty source.                                                    | Stage live root identities in the current scene lifetime and mark them; do not delete or create an undo entry.                                          |
| Paste success                                | Insert fresh independent identities in destination; keep payload for repeated Paste.                            | Move existing identities atomically; clear cut payload/marks once, after successful commit.                                                             |
| Paste rejection/no-op                        | No mutation; retain payload.                                                                                    | No mutation; retain staging so the user can choose another destination or cancel.                                                                       |
| Successful same-project scene switch         | Retain snapshot: copying A, loading B, then pasting is supported without keeping A loaded.                      | Cancel staging and clear marks. Cross-scene Cut is unsupported because it needs two authored scene transactions. Never silently convert Cut to Copy.    |
| Source deleted/reloaded                      | Snapshot remains usable if its typed references validate at Paste.                                              | Cancel if roots are deleted or the source lifetime is replaced. Revalidate parent/locks at Paste; unrelated scene edits do not invalidate it wholesale. |
| Successful project switch                    | Clear the Oxygen clipboard. Project-relative asset URIs must not bind silently to a different project's assets. | Cancel and clear; never mutate an unloaded project's scene.                                                                                             |
| Cancelled scene/project switch               | Retain unchanged snapshot.                                                                                      | Retain unchanged staging.                                                                                                                               |
| New Copy/Cut or explicit Escape cancellation | New capture replaces the old payload; Escape cancels staging, not text editing or unrelated system clipboard.   | Clear previous cut marks before staging the new roots.                                                                                                  |

This slice supports app-session node clipboard, not cross-application/project
import. Do not clear the Windows clipboard globally on project switch. Native
TextBox clipboard keeps precedence. If a Windows custom-format bridge is used,
keep the same service authoritative and validate payload version/project scope;
ordinary text/file formats are not node-transfer commands.

**Paste destination is explicit and predictable:**

| Current destination                          | Normal Paste                                                                                                                             | Paste Into / Paste as Child                                    |
| -------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| Scene root or no selection                   | Append at scene root.                                                                                                                    | Same destination.                                              |
| One folder                                   | Append into that logical folder, using its scene-parent scope for newly created Copy nodes. Cut grouping still cannot silently reparent. | Same destination.                                              |
| One node                                     | Insert after that node as a sibling in its visual container; do not accidentally make it a child.                                        | Explicitly insert under that node, preserving root world pose. |
| Several rows in the same visual sibling list | Insert after the last selected sibling in canonical order.                                                                               | Disabled: choose one container.                                |
| Multiple destination scopes                  | Disabled with “Choose one destination”; no arbitrary first node or silent root fallback.                                                 | Disabled.                                                      |

Toolbar/keyboard and context-menu Paste use the same resolver. Context-menu
selection, captured destination and dismissal follow the required
contextual-menu contract above; never resolve the target again from a later
selection after an await.

Both Paste and Paste Into preserve captured root **world** poses and descendant
local transforms. Cut uses the source's current accepted poses at commit; Copy
uses its snapshot poses. No automatic offset, snap-to-parent-origin or
coordinate unit conversion. Fresh root names may receive a predictable display
suffix for recognition; existing duplicate node names remain legal and never
establish identity.

Remap node/component/folder IDs only inside the copied closure. Keep geometry/
material/texture identities unchanged within the same project; do not import or
clone assets. Preserve material-slot IDs because they belong to the referenced
geometry, not the duplicated node. Source scene Environment is never included.
Copied directional lights become AtmosphereSlot.None, retaining other authored
light properties: duplication must not steal Primary/Secondary roles. Report
that role policy in command help/result, not as a silent conflict repair.
Same-scene external node references may remain if valid; cross-scene external
references reject the entire Paste with a field-specific explanation until an
explicit remap workflow exists. Unknown component/reference types must not be
guessed or dropped.

### Deletion and creation follow scene semantics, not a file manager

Recommend revising the earlier destructive folder-delete proposal: a logical
folder has no transform/domain object, so normal **Delete folder removes the
grouping only**, promoting its child entries in place and preserving graph/TRS.
Node Delete removes its authored subtree. Mixed Delete combines these meanings
once in one transaction; normalize overlapping node/folder closure so nodes are
not deleted twice. Reject protected/locked affected items atomically. If
deleting folder contents is desired later, expose an explicitly named
destructive command with a count/confirmation; do not make ordinary folder
Delete unexpectedly erase scene objects. Undo restores exact folder identity and
layout position.

New node under root/no selection uses root; under a node creates a child with
the domain's default local TRS; under a folder creates in that folder's actual
parent scope and groups it. New folder under a folder nests grouping; with a
selected node it is created as a sibling grouping in that node's parent scope,
not as a transform parent. Creation requires a single destination;
multi-selection creation is disabled rather than guessing a parent. Creation
defaults are different from preserve-world movement of existing objects.

## 5. Contract alignment required inside the implementation

Resolve these together in SE-01's design/API review, not through another
planning phase or by guessing mid-implementation. Update the owning LLD before
changing an accepted contract. The reviewer is the user or assigned maintainer.

| Conflict/dependency                                                                                                                    | Proposed resolution / decision owner                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| -------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scene Explorer LLD currently defaults to preserve-local reparent; brief requires preserve-world.                                       | Decided D1 (2026-10-04): preserve-local is the Explorer reparent default; explicit "Paste as child (keep world)" remains the only preserve-world path. The LLDs already state this (`scene-explorer.md:129`, `scene-authoring-model.md:47`); no contract change needed. Generic tree move APIs keep no transform policy.                                                                                                                                                               |
| Existing folder-lineage rules allow folder scope to trigger reparent; brief says grouping never reparents.                             | Decided D2 (2026-10-04): moving a node/folder into a folder in a different scene-parent scope reparents it; no grouping-only restriction. `scene-explorer.md:153` already permits lineage-driven reparent, so the contract agrees.                                                                                                                                                                                                                                                     |
| Root/folder/mixed context cannot travel through node-only selection messages.                                                          | Extend existing document selection owner with row-kind context and primary identity, then adapt Inspector/viewport consumers. Do not redefine an empty node list as every non-node case.                                                                                                                                                                                                                                                                                               |
| Settings LLD specifies Hide persistence but not complete Lock/picking placement.                                                       | Extend the same owning workspace LLD for per-project/scene lock and category preferences, using typed settings and existing project-lifetime rules. Prefer one coordinated workspace interaction service with typed substate; do not mix them into scene DTOs.                                                                                                                                                                                                                         |
| Folder delete/duplication has ambiguous old draft descriptions.                                                                        | Decided D5 (2026-10-04): folder Delete is grouping-only promotion that never deletes nodes and never changes scene-graph parentage or transforms; node Delete is subtree deletion; copied directional lights always reset `AtmosphereSlot = None`, knowingly accepting that Cut→Paste loses the assignment. The earlier destructive folder-delete recommendation stays rejected. Recording this in the owning contracts (plus one nested-promotion test) is the only outstanding work. |
| User requires one loaded scene and configured-default startup, but the manifest has no default field and startup restores usage state. | Projects/document owners add and validate DefaultSceneId, seed it during creation, and coordinate a single accepted scene lifetime. Align the project schema policy and startup/document LLDs before implementation. LastOpenedScene and ActiveScene's first-scene fallback cannot stand in for configuration.                                                                                                                                                                         |
| Copy must remain useful across sequential scene loading without enabling cross-scene Cut or cross-project asset rebinding.             | Reviewer approves the section 4 payload/destination/lifetime table: same-project snapshot Copy transfer, scene-bound Cut, clipboard cleared only after successful project switch. Use one existing intent path and no second history authority.                                                                                                                                                                                                                                        |
| Runtime managed APIs reviewed here lack complete hide/pick surfaces.                                                                   | Runtime owner verifies supported installed SDK capabilities and maps view-generation/node identity. If absent, implement the smallest owning Runtime/Interop/native capability with separate authorization/build evidence. SE-03 remains blocked for native behavior until available; mocked acknowledgments or authored visibility writes do not qualify it.                                                                                                                          |

The old source-adjacent REDESIGN/architecture/operations drafts contain obsolete
tree-first mutation advice. This plan replaces their execution checklist; linked
LLDs own the agreed contracts. Do not execute both plans or revive their direct
adapter/history paths.

## 6. Three large implementation slices

Each slice includes its tests, owning documentation and review. These are large
thematic deliveries, not a sequence of per-file pilots. Implement cohesive
internal steps without turning them into new milestone slices. No mandatory
subagents, old-UI baselines or repeated approval pauses for routine
implementation choices.

### SE-01 — Generic DynamicTree presentation and request pipeline

**Dependencies:** approved contract/API alignment from section 5 for changed
contracts. No engine build. **Deliverable:** a reusable tree capable of the
target row and interactions, demonstrated with a non-Oxygen consumer.

**Touch points:** DynamicTree/DynamicTreeItem control, properties/templates,
selection/filter/focus/clipboard/mutation paths, public events/contracts;
`projects/Controls/DemoApp/Tree/`; DynamicTree Models/UI tests and design docs.

1. Add generic trailing-content/template and optional metadata presentation
   inputs, forwarded from tree to rows, with optional reserved column width and
   resource tokens. Keep native row template parts, label editing, disclosure
   and focus. No default eye/lock content and no Oxygen-specific interface
   additions.
2. Replace fixed template row height with measured Auto/MinHeight behavior;
   connect font/padding/icon resources deliberately. Keep trailing actions
   aligned while indentation changes. Provide automation names/state for tree
   rows and usable action focus traversal without adding accidental duplicate
   tab stops.
3. Isolate interactive descendants from row selection/drag/typeahead. Make
   template replacement/recycling/unload idempotent: detach old events, restore
   current state and do not retain an old item's rename buffer, content or drop
   indicator. Support consumer-owned row/background context requests for
   pointer, keyboard and touch without hardcoding menu commands. Test an
   unrelated contextual menu and the no-menu case; text editors retain their own
   native context behavior.
4. Add a generic asynchronous rename commit path on the owning tree VM. Default
   standalone behavior can validate/set a label; application overrides submit
   domain commands and return acceptance/error text. The item captures original
   adapter/lifetime, prevents double commits and handles rejection visibly. Keep
   BeginRename usable after scroll/realization and offer a tree-level
   realize/focus/rename request so the Explorer does not search private
   templates.
5. Expose a generic awaited move/drop request seam before projection mutation.
   Reuse existing move validation/events where sufficient, but do not use async
   void cancellation callbacks as a transaction protocol. Default generic VM
   still performs its ordinary move; Oxygen override commits then reconciles.
6. Add Into to the generic indicator state/template. Reuse existing quarter-band
   hit testing and hover expansion; allow valid Before/After destinations across
   source parents. Centralize validation so drag preview and release share
   target semantics; final commit revalidates changed state. Clear cues on
   cancel/recycle. Include bounded edge scrolling, drag-cancel timer cleanup and
   request operation feedback. Copy-drop must not overwrite/consume the
   application clipboard, and DropCompleted must never repeat an already
   committed move/delete.
7. Keep clipboard shortcuts and toolbar requests on one overridable intent path.
   Generic clipboard remains generic; Oxygen can provide domain snapshots/moves
   without the tree calling CloneSelf on live scene objects behind its back.
8. Add explicit displayed-interaction scope for pointer/keyboard ranges, focus,
   typeahead and SelectAll. Preserve current ShownItems default and its tests.
   Selection stores canonical items/indices; translate displayed range endpoints
   to items, never use a filtered index as a source index. Explorer will opt in.

**Acceptance:** Models/UI suites cover two unrelated fake item types, empty and
interactive trailing slots, default no-slot layout, enlarged text, Light/Dark/
contrast semantics, rename accept/reject/cancel/stale completion, drop cues and
batch order, filtered range/index correctness, focus and recycled rows. Controls
DemoApp consumes the hooks with non-Oxygen data/actions. DynamicTree has no
Oxygen reference and no alternate application-specific tree implementation.

**Status (corrected 2026-10-04 per P1):** SE-01 is **in progress**, not complete
— C32 (recycling detach / drop-state reset), C34 (displayed-scope keyboard and
typeahead), C35 (drag cancellation), C38 (inert `HideChildrenAsync` guard) and
the T9/T10 verification gaps remain open. What is done below stands; the
completion claim did not. The direct trailing template takes precedence over
selectors; fixed-width empty slots and recycled content are covered. Demo-owned
lock/visibility hover commands and loaded-status cells remain aligned across
depths and densities. Final review corrected passive status content intercepting
row input and made hover subscriptions idempotent. The UI project builds without
warnings with analyzers enabled; 151 model and 89 UI tests pass. User demo
acceptance remains pending. Exhaustive contrast/DPI/text-scaling and workload
qualification was not rerun in this closeout; editor/runtime qualification
remains part of SE-03. No engine build or production editor integration is
claimed.

### SE-02 — Scene Explorer domain projection and atomic authoring

**Dependencies:** SE-01. **Deliverable:** stable, document-scoped Explorer state
and correct create/rename/delete/move/clipboard operations, including Inspector
context.

**Touch points:** `WorldEditor/src/SceneExplorer/` adapters, Operations and
Services; `src/Documents/Commands/` and `src/Documents/Selection/`; World
DTO/serialization only if an approved contract needs it; Inspector selection
consumer/composition; Projects model/manifest/context/creation/validation and
tests; WorldEditor Workspace/DocumentManager activation, App
ProjectActivationCoordinator and metadata-copy consumers; WorldEditor
Unit/Unit.UI/Integration.UI tests and owning LLDs.

1. Replace the current giant-VM responsibilities with a few concrete
   collaborators: projection/index/lifetime owner; document selection bridge;
   hierarchy command implementation. Keep SceneExplorerViewModel derived from
   DynamicTreeViewModel as the consumer orchestrator. Avoid a parallel framework
   or extra DI container. Coordinate one accepted loaded scene through the
   existing project/document lifetime owner, using SceneAuthoringGate and
   dirty-close guards where applicable. Remove constructor-captured project
   ownership and premature competing ActiveScene assignments. Material-tab
   activation and background document opens do not load scenes. Add
   DefaultSceneId and complete its serialization/context/template/validation
   path; resolve startup from that configuration, never last-opened or
   first-listed. Handle missing configuration explicitly and keep schema/version
   policy owner-approved.
2. Build one scene node lookup and stable node/folder adapter index. Use
   persisted layout ordering and true scene ancestry; reconcile missing/stale
   layout entries without losing nodes or creating duplicates. Do not scan
   AllNodes for each row, force-load arbitrary generic trees, or rebuild the
   entire tree for every rename. Dispose adapter model observers on
   removal/reload and suppress old queued callbacks.
3. Expose scene kind, count, glyph, search label and effective workspace-state
   inputs on Oxygen adapters. Domain Label is read-only presentation until an
   approved command succeeds; remove the post-hoc label-change/history bridge.
4. Extend the existing document command owner with typed, identity-based
   hierarchy requests/results. SceneExplorerService orchestrates
   Mutator/Organizer within that path, not as another history/dirty authority.
   Capture immutable context, selection roots, target/sibling index and
   graph/layout/transform before/after.
5. Prevalidate the complete batch, commit graph and layout together, advance
   scene revision/dirty/history synchronously, reconcile projection, then
   request native convergence. Undo/redo use the same apply path and restore
   identities, exact sibling order, local transforms and layout. No
   before-command UI deletion/move, fire-and-forget backend mutation or
   duplicate VM history entries.
6. Implement preserve-world reparent and grouping-only folder move from section 4. Resolve Before/After insertion against underlying siblings, including
   filtered or collapsed destinations. Cut/paste and drag share this
   implementation. Enforce cycles, locks, scope and ownership on programmatic
   commands too.
7. Replace shallow adapter clipboard with authored snapshots for Copy and
   captured root identities for Cut. Normalize ancestor/descendant selections;
   preserve subtrees/order; generate independent identities on each Copy/Paste;
   stage cut until successful Paste. Retire the Explorer's Copy+Delete shortcut.
   Implement the precise normal Paste/Paste Into destination resolver, retained
   repeated Copy payload, same-project cross-scene snapshots and reference/role
   rules in section 4. Cancel Cut only on committed scene replacement; clear the
   Oxygen clipboard only on committed project replacement, not on cancelled
   guards. Implement grouping-only folder Delete and node-subtree Delete
   distinctly.
8. Extend selection with scene/folder/node/mixed context and explicit primary.
   Subscribe the Explorer to external selection changes without feedback loops.
   Root → Environment, folder → summary, mixed → summary/aggregate; do not
   publish only the node subset as a misleading exclusive Component Inspector
   selection. Settle active inspector text/drag according to existing edit
   policy before replacing its targets. Preserve valid selection across
   reorder/undo/reload.
9. Add scene-search presentation using the scene index plus existing generic
   filtering: find collapsed descendants, expand paths temporarily, restore
   prior expansion when cleared, preserve selected identities outside results.
   Temporary search expansion must not write ExplorerEntryData/SceneNode
   expansion or dirty a document. Expose match count excluding context-only
   ancestors.
10. Wire typed command availability to complete
    selection/lifetime/lock/clipboard state. Rename uses SE-01's inline hook;
    deletion/clipboard cannot steal text editor keys. Replace reflection, direct
    domain setters and stale-history fallback. Add the context-aware action
    definitions, immutable menu context and typed invocation adapter from
    section 4. Implement Remove from folder/Move to scene root with the existing
    grouping/move command path, not new mutation logic. Unit-test menu shapes
    and full-selection eligibility for all row kinds.

**Acceptance:** command and native UI tests prove complete operations, one-step
undo/redo, rejected batch no-op, Save/reopen, protected descendants, actual
order, new copy identities, Cut cancellation, stale document rejection, failed
sync without authored rollback, collapsed-match search and root/folder/mixed
Inspector context. Configured-default startup/explicit override, guarded scene
replacement, copy A → load B → paste and cancelled switches are covered.
Projects model/manifest and existing inspector/scene-document regressions pass.
No hidden direct mutation or separate selection/history/clipboard authority
remains.

**Progress (2026-10-04):** the atomic-authoring foundation is landed on the
document command owner. `ISceneDocumentCommandService` now exposes typed,
identity-based hierarchy commands (`CreateNodeAsync`, `CreateFolderAsync`,
`RenameNodeAsync`, `RenameFolderAsync`, `DeleteNodesAsync`, `DeleteFolderAsync`,
`ReparentNodesAsync`, `MoveNodesToFolderAsync`, `RemoveNodesFromFolderAsync`)
implemented in `SceneDocumentCommandService.Hierarchy.cs` with whole-batch
prevalidation, atomic graph+layout commit, synchronous history/dirty advance and
native convergence. Corrected 2026-10-04 (P8): grouping is **not** always
Parent/TRS-neutral — under D2 a move into a folder whose scene-parent scope
differs performs the lineage-driven reparent inside the same command
(`MoveNodesToFolderAsync`), and under D1 the Explorer default is
**preserve-local**, not preserve world pose (`SceneExplorerViewModel` passes
`preserveWorldTransform: false`); "Paste as child (keep world)" is the only
preserve-world path. The transform math itself is unchanged: decompose/recompose
via `SceneTransformMath` with singular/shear rejection and redoes the forward
move rather than re-guessing. `SceneDocumentCommandService` now owns
`ISceneMutator`/`ISceneOrganizer` directly. Reconciled 2026-10-04 (P2): the
atomic commit claim now holds — C8, C9 and C10 are closed with whole-batch
prevalidation, single-undo and mixed-delete regression coverage.

The single-loaded-scene policy is **partly** in place (corrected 2026-10-04 per
P3 — C30 has no guard/stage/retire on scene replacement and C31's creation seed
is unimplemented, so startup is not yet wholly
configuration-driven):`DefaultSceneId` is persisted in the project
manifest/model and propagated through `ProjectContext`; workspace startup
resolves the initial scene from it (after any explicit activation request) and
no longer falls back to `LastOpenedScene` or first-listed discovery; the
competing `ActiveScene` writes in `SceneExplorerViewModel`/`DocumentManager` are
removed.

Rename is routed through the command owner: `Scene`/`SceneNode`/`Folder` adapter
labels are read-only presentation, `SceneExplorerViewModel.CommitRenameAsync`
submits node/folder renames through the command service, and the post-hoc
label-change/history bridge plus the direct `AttachedObject.Name` writes are
gone. The in-place rename editor is re-anchored over the item name (dynamic-tree
fix).

The stable projection index and drag/drop commit-hook are in place: the VM now
keeps a node **and** folder adapter index (lookups no longer scan the tree or
force-load collapsed subtrees), `SceneNodeAdapter` detaches its model observer
on removal/reload, and `CommitDropAsync` submits node drops (Into
root/node/folder) through the command owner (`ReparentNodesAsync` with
preserve-world, or `MoveNodesToFolderAsync` for grouping) then reconciles the
projection. The six `RunTreeMutationAsync` wrappers and the tree-first
`OnItemMoved` backend/history handler are removed; moves are command-first.
Create (`AddEntity`/`CreateFolder`) and delete (`RemoveSelectedItems`) are now
also routed through the command owner
(`CreateNodeAsync`/`CreateFolderAsync`/`DeleteNodesAsync`/`DeleteFolderAsync`)
followed by projection reconciliation, and the tree-first `OnItemAdded`/
`OnItemRemoved` backend/history handlers plus the shallow adapter clipboard are
removed — completing atomic authoring for create/delete/move. The organizer's
layout overlay now initializes lazily from the root nodes instead of throwing on
a freshly loaded, layout-less scene (fixes the `ExplorerLayout is not
initialized` failure on the legacy tree-first create-folder path).

Clipboard and selection context are landed: `ISceneSelectionService` now carries
a typed `SceneSelectionContext` (scene/folder/node/mixed kind plus primary
identity) published by the Explorer alongside the node subset, and
`DuplicateNodesAsync` deep-copies node hierarchies with fresh node/component
identities while preserving asset and material-slot references. Copy snapshots
node identities, Cut stages them as a move, and Paste duplicates or moves at the
resolved destination. Scene name search is landed
(`SearchAsync`/`ClearSearchAsync`): it matches node and folder names through the
scene index, expands matching nodes' scene ancestry to reveal collapsed
descendants, and restores the prior expansion on clear.

Step 6 is complete: `MoveFolderToParentAsync` (grouping-only folder reparent),
`ReorderNodesAsync`/`MoveNodeToSiblingIndex` (layout-only sibling insertion with
source-removal index adjustment) and copy-drop via `DuplicateNodesAsync` cover
every drop intent (Into root/node/folder, Before/After reorder and Ctrl-drag
copy). Reconciled 2026-10-04 (P4): C6 and C13 are closed, and
`SceneExplorerDropTests` pins the preserve-local (D1) routing that this claim
depends on. Step 10 landed typed command availability (the view reflection
helpers are removed), the shared action definitions and menu-shape builder
(`SceneExplorerContextMenu`), and the `SceneExplorerCommandAdapter` with
Remove-from-folder/Move-to-scene-root hierarchy commands. Step 1 extracted the
adapter index/tracking into a `SceneExplorerProjection` collaborator, shrinking
the consumer orchestrator and removing the remaining tree-first mutation paths.

The keyboard clipboard path is also routed through the command owner: the base
`DynamicTreeViewModel` clipboard methods (`CopyItemsAsync`/`CutItemsAsync`/
`PasteItemsAsync`) are now `virtual` with protected store accessors, and the
Explorer overrides them to stage node identities and paste through
`DuplicateNodesAsync` (deep copy) or `ReparentNodesAsync` (cut), instead of the
generic `ICanBeCloned` shallow copy that reused the same `SceneNode`. All async
continuations that mutate UI-bound tree collections now use
`ConfigureAwait(true)`, fixing the `RPC_E_WRONG_THREAD`/`0x80010117` crash where
`ReconcileProjectionAsync` touched an `ObservableCollection` from a thread-pool
thread after a command await.

Evidence: 68 Projects tests and 265 WorldEditor Unit tests pass (drop-hook,
folder-index, layout-initialization, command-routing, selection-context,
deep-copy, keyboard-clipboard routing, search, reorder/folder-move and
context-menu cases); WorldEditor src, Unit, and Oxygen.Editor.App build clean.
An independent adversarial review (2026-10-04) found SE-01 and SE-02 **not
complete**: several source-level correctness defects and unapproved contract
decisions remain. They are tracked in the corrective-action checklist below and
must be closed before SE-03. Restated per P6 — there is no deferred
`AuthoringChanged` event to wire: `SceneContentDemandService` observes the model
directly. It subscribes `RootNodes` and, per node, `Children`, `Components` and
each `GeometryComponent.OverrideSlots`
(`SceneContentDemandService.References.cs:24-32`); `ObserveCollection` attaches
`CollectionChanged += OnReferencesChanged` (`:50-54`) and `OnReferencesChanged`
dispatches `RefreshReferenceObservers` (`:59`), while property edits go through
`OnReferenceChanged` → `InvalidateObsoleteDemands` (`:58`). Any writer that
mutates those collections therefore re-subscribes the observers without an
authoring signal, which is why no `AuthoringChanged` hook is needed. What is
unproven is that a **command-driven** mutation reaches this path — that is a
test gap, recorded as T12, not an integration note to carry into SE-03.

### SE-02 corrective actions — review findings (tracking)

Legend: `[ ]` open · `[~]` in progress · `[x]` closed. Each entry names owner +
evidence. A `[~]` decision row must state the decision once (no second section
restating it) and name the work item that implements what is left — a decision
with no queued work is mislabelled `[ ]` blocked, not `[~]`. An item closes only
when its code, its contract-doc recording and its regression tests are all done;
"code landed" alone never does.

Verification evidence (2026-10-04): `Oxygen.Editor.WorldEditor.Unit.Tests` full
project green — 313 total, 313 succeeded, 0 failed, 0 skipped (272 baseline +
five search/layout tests with C25/C26/C27, two delete tests with C42, five
failure-visibility tests with C40/C41/C43/D-f, twelve primitive-shape tests with
C48), plus `Oxygen.Managed.Core.Tests` 95/95 (`traverse Invoke-Tests --start
projects/Oxygen.Editor.WorldEditor/tests/Unit --configuration Debug`). The
strict-mock helper `ConfigureHierarchySync` previously omitted the native rename
and transform publishes added with C2/C1/C3, so four tests threw while their
defects were marked closed.

#### Owner-approval blockers (cannot be implemented without a recorded decision)

- [x] D1 — projects/docs. Decision landed and asserted
      (`CommitDropAsync_RoutesNodeDropIntoNodeThroughReparentCommand` expects
      preserve-local). The original citation was stale: `scene-explorer.md:129`
      already states "ED-M03 default is preserve local transform" and
      `scene-authoring-model.md:47` already states "Reparenting preserves local
      transform; an existing explicit preserve-world operation remains
      distinct", both predating this milestone. The plan's conflict row now
      records the decision instead of the superseded preserve-world proposal, so
      no contract change was needed.
- [x] D2 — projects/docs. Decision landed (`cbd1d2372`, folder moves reparent).
      The cited `scene-explorer.md:153` already permits lineage-driven reparent,
      so the contract agrees; the plan's conflict row now records the decision
      instead of the superseded grouping-only restriction.
- [~] D3 — **decided 2026-10-05 by the orchestrator under the owner's V0.1
  delegation** (previously mislabelled "unapproved"): selection carries a row
  kind (node / folder / mixed / root) and an explicit primary identity rather
  than deriving primary from row order; a folder or mixed selection shows the
  summary surface, the root row shows scene Environment, and an empty node
  subset never means "every non-node case". Remaining: the code
  (`documents-and-commands.md:150` still publishes node-only; Inspector consumes
  nothing — C16–C19) and the LLD wording in the docs pass.
- [~] D4 — **decided 2026-10-05 under the same delegation**: workspace
  persistence covers Hide, Lock and category/view-column placement as typed
  per-project settings with scene-scoped lifetime, one coordinated workspace
  interaction service owning typed substate — not new fields on scene DTOs.
  Remaining: none of it exists yet (`settings-architecture.md:65` is Hide-only),
  so this is unimplemented work, not an open question. **Work: C49.**
- [x] D5 — **decided by the owner (2026-10-04)**, both clauses: folder Delete =
      grouping-only promotion that never deletes nodes and never changes
      scene-graph parentage or transforms; node Delete = subtree deletion;
      copied directional lights always reset `AtmosphereSlot = None`, with the
      Cut→Paste assignment loss accepted knowingly. Code and tests landed
      (`Clipboard.cs:257` with
      `DuplicateNodesAsync_ResetsCopiedDirectionalLightAtmosphereSlot`;
      `DeleteFolderAsync` at `SceneDocumentCommandService.Hierarchy.cs:295` with
      `DeleteFolderAsync_PromotesContainedEntriesWithoutRemovingNodes`).
      **Contract recording is done** (owner approved the wording 2026-10-04):
      `scene-authoring-model.md` §6 now states the duplication rule next to the
      existing single-occupant rule, and `scene-explorer.md` §7 states folder
      Delete as layout-only promotion that leaves parentage and world pose
      untouched. T11 closed the last evidence gap on 2026-10-05 (`bff041741`),
      so decision, code, contract and tests are all in place.
- [~] D6 — **decided and already acted on**: keep the one-time `LastOpenedScene`
  migration and the first-scene fallback as recovery paths, persist
  `DefaultSceneId`, and seed it at project creation; usage history may remain
  but never overrides the configured default. The field, serialization, context
  propagation and startup resolution all landed earlier; the only open piece was
  the creation seed, which is being implemented now (task #7 / C31) on the basis
  of this decision — noted here because I dispatched that work before confirming
  provenance.
- [~] D7 — **decided 2026-10-05 under the delegation**: Copy transfers a
  same-project immutable snapshot (never live adapters or source-only ids), Cut
  is bound to the scene it came from, and the clipboard is cleared only on a
  successful project switch, with one intent path and no second history
  authority. Remaining: snapshot Copy landed (C11); the switch-invalidation
  protocol is unwired and the payload still carries source ids, and `plan:568`
  needs the LLD wording. **Work: C50** (plus the `plan:568` wording in the docs
  pass).
- [x] D8 — **decided by the owner and ratified in the analysis this row points
      at**:
      [`D8-show-in-editor-runtime-capability.md`](../review/D8-show-in-editor-runtime-capability.md)
      owns the behaviour contract (R1–R9), the design decisions (DD1–DD4), the
      per-team action plans (E1–E3 engine, I1–I4 interop/runtime, W1–W7 editor)
      and the qualification cases. This row does not restate them. What binds
      the plan: - **D8a** is a per-view hidden-node filter on `CompositionView`,
      culled in `ScenePrepPipeline::PrepareView`. An editor-hidden node **does
      not render and does not cast shadows** in editing views (R2) — it is
      dropped, so no draw, no depth, no shadow-caster source; the UE5.7 parity
      basis is `bHiddenEd`, which removes the actor and its direct shadows from
      the editor viewport without touching runtime state. The existing
      shadow-only route (`main_view_visible = false`) is deliberately **not**
      used, because it exists to keep off-frustum casters shading the scene and
      a hidden node must stop shading it. No depth-prepass change is required
      for this, since dropped geometry never reaches the prepass. -
      Editor-hidden nodes stay **fully visible in game, in game viewports, in
      cooked assets and in capture/validation views** (R3/R6). The mask never
      writes `kVisible`, never dirties, never records history and never cooks
      (R4) — which is why the eye must never be routed through
      `RuntimeSetVisibility`/`SetVisibilityCommand`. - Inheritance follows
      **actual scene ancestors only**, never logical folders (R1); the editor
      sends hidden roots and the engine resolves the closure (DD2), as a
      revisioned per-activation snapshot applied to every editing viewport of
      the active scene (DD3, R6). - **D8b** is ED-M09's work: picking does not
      exist at any layer. The Explorer contributes category and hidden-set
      filters on each pick request (DD4); hidden nodes cannot be picked in
      viewports (R8) yet Explorer selection still works, and a hidden node
      selected individually in the Scene Explorer shows its transform gizmo at
      its world transform (R9, W7 — owner decision on Q4). - Editor side (C49 /
      W1–W4) is **unblocked and buildable now**: workspace service,
      `WorldEditor/SceneVisibility` persistence, the eye/lock slots and the
      binding with the `SupportsEditingViewMask` warning fallback (Q2 — the eye
      works immediately and logs a diagnostic while suppression awaits I4).
      Engine and interop work needs separate authorization and build evidence,
      so it is not scheduled from this plan.

#### Show in Editor & Lock — interaction contract (owner, 2026-10-05)

Behaviour to deliver, not a prescription for how to build it; the tree surface
is DynamicTree, implemented the way the controls demo app drives it.

- Two states, fully independent of each other and of runtime visibility: **Show
  in Editor** (an eye icon) controls viewport presentation _while editing_ — it
  has zero effect on runtime visibility, cooked assets or game rendering.
  **Lock** (a lock icon) controls editability, preventing accidental
  manipulation. Neither implies the other in either direction.
- The row's trailing area reserves two fixed-width slots, vertically aligned
  across every row in the tree: slot 1 = Show in Editor, slot 2 = Lock. Showing
  or hiding an icon must never collapse its slot or shift neighbouring icons,
  text or buttons. Lock never migrates into slot 1 and vice versa.
- Default states are quiet; suppressed states are loud. When an item is Shown in
  Editor and Unlocked, both slots are empty until the pointer enters the row, at
  which point the interactive Eye and UnlockKeyhole appear at muted opacity.
  When a state is suppressed — Not Shown in Editor, or Locked — that slot holds
  its icon **permanently**, regardless of pointer or selection: EyeOff as a
  warning that the object is not visible in the editor viewport, LockKeyhole in
  an active/accent treatment as a warning that the item is protected. All four
  hover/permanent combinations of the matrix are required.
- Pointer exit removes only the default-state affordances immediately. A click
  toggles its own state at once and must never leave the sibling icon pinned
  open — the two slots' hover lifetimes are separate.
- Hover-revealed controls are input targets; permanently visible status icons
  are indicators. Passive status content must not intercept row input (the SE-01
  review finding applies to these slots), and hover subscriptions must be
  idempotent under row recycling.

What is gated: the Lock half and both states' persistence are pure editor-side
work (C49). Applying "Not shown in editor" to the viewport needs the
editing-view mask (D8a). Build the state, its storage, the row UI and the single
owning API now (C49); per owner decision Q2 the eye operates immediately,
maintaining workspace state and persistence and logging a diagnostic warning
while native viewport suppression awaits D8a/I4. Inheritance follows **actual
scene ancestors only** — logical folders never contribute (R1) — and `Show All`
clears the current scene's set (W2).

#### Correctness defects — editor/controls

Transforms, reparent and native convergence

- [x] C1 — `SceneTransformMath.cs:66` divides by the new parent world even when
      `node.IgnoreParentTransform` (verified counterexample: world X=0 under
      X=10 parent → −10). Preserve the node's world (=local) unchanged instead.
- [x] C2 — `Hierarchy.cs:193` `RenameNodeAsync` records history/dirty but no
      native rename sync.
- [x] C3 — `Hierarchy.cs:400` + `SceneEngineSync.cs:490`: preserve-world updates
      managed local TRS, then reparents native with
      `preserveWorldTransform:false` without publishing the new TRS.
- [x] C4 — `Hierarchy.cs:605` delete undo restores the root graph but
      native-creation only for the restored root, not its subtree.
- [x] C5 — `Hierarchy.cs:640` undo reparents the native node to the forward
      destination, not the restored parent.
- [x] C6 — `SceneExplorerViewModel.cs:1339` reorder swaps node/folder tuple
      values.
- [x] C7 — `SceneOrganizer.cs:612,323` layout mutates before destination
      validation; a rejected move alters authored layout.

Atomicity (one history step per batch, all-or-nothing)

- [x] C8 — `Clipboard.cs:65` duplication records undo per root; a later invalid
      root leaves earlier changes committed as separate history entries.
- [x] C9 — `SceneExplorerViewModel.cs:189` mixed node/folder delete submits
      separate commands.
- [x] C10 — `Hierarchy.cs:419` grouping loops mutate incrementally without batch
      rollback.

Clipboard

- [x] C11 — `SceneExplorerViewModel.cs:503` Copy captures IDs/live adapters, not
      immutable DTO/layout/pose snapshots; folders excluded; sequential-scene
      Copy unresolved.
- [x] C12 — `Clipboard.cs:108` copied directional lights retain `AtmosphereSlot`
      (plan: `None`).
- [x] C13 — `SceneExplorerViewModel.cs:550,650` normal Paste on a node = child
      Paste; Paste and Paste-as-child identical; multi-scope silently falls back
      to root.
- [x] C14 — `SceneExplorerViewModel.cs:586` successful Copy-paste clears the
      payload; Cut into a folder loses grouping intent.
- [x] C15 — base clipboard Escape cannot cancel a subclass's private IDs.

Selection

- [ ] C16 — `SceneExplorerViewModel.cs:1059` publishes node-subset only (folder
      → empty list → Environment; mixed → nodes → Component Inspector).
- [ ] C17 — primary identity derived from row order, not an explicit active
      identity.
- [ ] C18 — selection service has no change notification consumed by Explorer;
      rebuild does not reselect by identity.
- [ ] C19 — root exclusivity not implemented.

Context menu

- [ ] C20 — `SceneExplorerViewModel.cs:618` builds from current selection; no
      frozen document/lifetime/selection invocation context.
- [ ] C21 — `SceneExplorerViewModel.cs:637` most actions return
      current-selection commands; toolbar Click bypasses shared CanExecute.
- [ ] C22 — no production caller mounts row/background menus;
      keyboard/touch/stale-dismiss absent.
- [ ] C23 — `SceneExplorerContextMenu.cs:74` primary-only booleans don't
      establish batch eligibility.

Search / index

- [x] C24 — `SceneExplorerViewModel.cs:944` second query discards restoration
      bookkeeping.
- [x] C25 — search expands only node ancestry, not visual folder ancestry /
      nested folders. (`SceneExplorerProjection.GetNodeLayoutAncestors` returns
      each match's authored-layout chain — folders and node seats — which
      `SceneExplorerViewModel.ExpandMatchAncestryAsync` expands top-down,
      falling back to scene-graph ancestry only for nodes with no layout seat.
      Proven by `SearchAsync_RevealsNodeInsideCollapsedNestedFolders`, which
      expands and then restores both an outer and an inner collapsed folder.)
- [x] C26 — adapter-tree index is not a complete domain lookup;
      `SceneAdapter.cs:175` scanned per entry; duplicate layout ids not
      suppressed. Node side: `Scene.AllNodes` indexed once per rebuild and node
      duplicates claimed before realization (`c381888a9`). Folder side: the
      projection now owns a domain view built from `Scene.ExplorerLayout`
      (`LayoutFolders` plus per-node ancestor chains) that is independent of
      adapter realization and expansion state, and Case A claims `FolderId` in a
      `seenFolderIds` set before realizing, so a duplicate entry cannot produce
      two adapters sharing one identity and contributes no children — the same
      claim rule as nodes, matching "duplicate/ambiguous persistent IDs are
      errors" (`content-pipeline.md:1205`). Proven by
      `Children_LayoutReferencingSameFolderTwice_RealizesSingleAdapter` and by
      the folder-match count taken from the domain
      (`SearchAsync_CountsCollapsedFolderMatchesFromLayoutDomain`). An entry
      with a null `FolderId` is not stably addressable: it still realizes under
      a fresh id each rebuild, which search now tolerates
      (`SearchAsync_FolderEntryWithoutFolderId_ToleratedBySearchAndClear`).
- [x] C27 — `FolderAdapter.cs:62` authored expansion setter used by search.
      `SetExpansionTransient` is exercised for real: set before a search-driven
      expand, cleared only after the collapse in `ClearSearchAsync`, so search
      never writes `entry.IsExpanded` and never dirties the document — asserted
      by
      `SearchAsync_RestoresExpansionAcrossSuccessiveQueriesWithoutAuthoringLayout`,
      which runs two queries, keeps the first query's expansion until clear,
      leaves authored expansion alone, compares the serialized layout
      before/after and checks `Metadata.IsDirty` is false.
- [ ] C37 — `ReconcileProjectionAsync` during an **active** search re-authors
      transient search expansions into `entry.IsExpanded` on the freshly
      realized adapters (no transient flag survives a reload) and leaves
      `searchExpandedItems` referencing retired adapters, so clearing cannot
      restore the prior view. Needs a reconcile-side decision; found while
      closing C27 and deliberately not papered over inside the search path.

Scene lifecycle

- [x] C28 — `SceneExplorerViewModel.cs:104` captures the constructor project.
- [x] C29 — `SceneExplorerViewModel.cs:747` background scene opens still load
      scenes.
- [ ] C30 — no guard/stage/retire on scene replacement;
      `SceneEngineSync.Documents.cs:17` retains editable graphs.
- [x] C31 — `ProjectCreationService.cs:237` had no default seed;
      `WorkspaceViewModel.cs:427` LastOpenedScene migration; `Project.cs:26`
      first-scene fallback; `AssetsViewModel.cs:515` premature ActiveScene
      write. (D6: migration + fallback approved; premature write fixed; creation
      seed landed 2026-10-05 (`f8721d187`): the service reads the copied starter
      scene's own serialized id and records it as `DefaultSceneId`, leaving the
      property null rather than guessing when the id is absent or unparseable;
      the `LastOpenedScene` migration stays as the owner's approved recovery
      path. D6 still needs its LLD wording (docs pass).

Generic tree

- [ ] C32 — `DynamicTree.cs:919` recycling does not fully detach subscriptions /
      reset drop state.
- [x] C33 — demo compact profile is 28/12 DIP, not required 32/14; comfortable 40.
- [ ] C34 — `DynamicTreeViewModel.cs:438,694` displayed-scope keyboard/typeahead
      scan hidden items.
- [~] C35 — `DynamicTree.cs:1567` Escape clears only typeahead; drag
  cancellation/restoration absent.
- [x] C36 — `ContextMenu.cs:119` attached host disposed on unload without Loaded
      recreation.
- [ ] C38 — `DynamicTreeViewModel.cs:933-934` `HideChildrenAsync` guard is
      inert: `removeIndex` is `shownItems.IndexOf(item) + 1`, so a missing item
      yields `0` and `Debug.Assert(removeIndex != -1, "expecting item … to be in the
shown list")` still passes; the assertion can only fire when `IndexOf`
      returns `-2`, which is unreachable. It must test `removeIndex != 0`. Found
      while closing C27; belongs with the C34/C35 generic-tree work rather than
      the search group.

Command surface and result publication (found by the P7 design review, each
re-verified)

- [x] C39 — `SceneExplorerService` was a **second mutation surface with no
      history authority** — narrowed 2026-10-04 by `e4eaf2f44`: the uncalled facade
      (create node, create folder, move, update-moved, rename, four private helpers,
      17 orphaned log methods) is deleted, and what survives is
      `AddNodeAsync`/`DeleteItemsAsync`, which now leave `AuthoringChanged` with
      **no production publisher** and still carry the D5-contradicting
      `promoteChildrenToParent: false` branch. Left deliberately as a unit rather
      than half-removed: retirement is tracked with its consumer repoint, not
      patched in dead code. Closed 2026-10-05 (`9d54484ea`): the service, its interface,
      log helpers, event args and DI registration are deleted, the D5-violating branch
      went with them, and `SceneSaveRevisionTests` now drives the real command service.
      Original evidence: zero `AddChange`/`BeginChangeSet`
      calls in the file, still DI-registered (`WorkspaceViewModel.cs:202`), and its
      folder delete calls `sceneOrganizer.RemoveFolder(..., promoteChildrenToParent:
false)` (`SceneExplorerService.cs:272`), which **discards** contained entries
      — the opposite of the D5 rule just recorded in `scene-explorer.md:105-108`.
      Its only remaining src caller is the dead `RenameItemAsync`, but its
      `AuthoringChanged` event is consumed by
      `SceneContentDemandService.Lifecycle.cs:33,42`, so pruning needs that consumer
      checked first. Doc §5:97-99 forbids this shape.
- [x] C40 — duplication and paste failures publish **no operation result**.
      Closed 2026-10-05 (`ce3538328`): all eleven paths publish through
      `PublishSceneFailure` under a new `Scene.Node.Duplicate` kind,
      `STALE_TARGET` for resolved-target failures plus a new
      `OXE.SCENE.AUTHORING_SUSPENDED` where the scene is reloading or was
      replaced (no precedent code existed; sibling commands still return bare
      failures there, so this is clipboard-only rather than a cross-command
      change). The view model was deliberately not touched, on evidence: it has
      no failure-surfacing mechanism at all, and every other failed command from
      it publishes through the command service into the Operations output
      channel while the view model merely aborts — paste already aborted
      identically, so publication was the only missing half. Original claim:
      every failure path returns `SceneCommandResults.Failure<T>()`
      (`Clipboard.cs:34,40,49,59,72,...`) and that helper defaults
      `operationResultId` to null (`SceneCommandResults.cs:26`), with the view
      model swallowing the failure (`SceneExplorerViewModel.cs:591-602`,
      copy-drop `:1386-1388`). `documents-and-commands.md` §12:340-341 requires
      a visible result for user-triggered failures. Also: duplication has no
      kind in `SceneOperationKinds` at all.
- [x] C41 — `"Scene.Reload"` was a bare string literal (`Reload.cs:63,103`)
      outside the stable vocabulary. Closed 2026-10-05 (`ce3538328`) with
      `SceneOperationKinds.Reload` and a pinned vocabulary test; one further
      literal that merely duplicated an existing constant
      (`SceneEditorViewModel.cs:532` → `SceneOperationKinds.Save`) went with it
      as a zero-delta correction.
- [x] C43 — a rejected mixed delete still mutated the scene. `DeleteItemsAsync`
      called `EnsureExplorerLayout` before folder prevalidation, so a
      `STALE_TARGET` rejection on a layout-less scene left the seeded layout
      behind — not dirty, not undoable. Closed 2026-10-05 (`ce3538328`) by
      seeding only after every validation passes, proven by
      `DeleteItemsAsync_WhenFolderIsStale_LeavesExplorerLayoutUnseeded` (red
      before, green after).
- [x] C44 — four `UpdateNodeTransformAsync` calls discarded their
      `Task<SyncOutcome>` (`SceneDocumentCommandService.Hierarchy.cs:449, :553,
:770, :787`). Deferred work returns an immediate `SkippedNotRunning` whose
      eventual replay failure _is_ visible through `PublishReplayFailures`, but
      a synchronous `Rejected`/`Failed` from dispatch or interop is published
      nowhere: the managed edit stands while native silently diverges. Same
      defect class as the rename path closed today, and not a one-line
      consequence of it — it needs the operation id threaded into
      `ReparentNodesAsync`/`MoveNodesToFolderAsync` results, the fire-and-forget
      site at `:553` restructured, and a decision on how undo/redo callbacks
      (bare `Task`) report failures. Closed 2026-10-05 (`bff041741`): all four
      publish through `PublishSyncOutcomeAsync` under the initiating command's
      kind, the first id threaded into the result; `MoveNodesToFolderAsync`
      became genuinely async to carry it; undo/redo publish from inside the
      delegate with no history-signature change, since the operations channel is
      initiator-agnostic — a test observes exactly one publication from undo and
      one from redo.
- [x] C45 — `CreateFolderAsync` had the C43 shape but cannot be fixed by the
      same move: the seed at `Hierarchy.cs:128` must precede
      `sceneOrganizer.CreateFolder`, which calls `RequireLayout`, so a stale
      `parentFolderId` throwing `InvalidOperationException` leaves the seed
      behind while publishing `CREATE_FOLDER_FAILED`. Fixed 2026-10-05
      (`bff041741`) by validating `parentFolderId` before seeding and returning
      a published `STALE_TARGET`, so a rejected create leaves no mutation
      behind. Its twin for `parentNodeId` is C51.
- [x] C46 — vocabulary sweep, decided rather than escalated: `"Scene.Mutation"`
      (`SceneEngineSync.Deferred.cs:139`) is published to the user with no
      constant, so it gets one; `EditSessionToken.OperationKind` carries
      `"Scene.Environment.Edit"`/`"Scene.Property.Edit"` that no code reads or
      publishes, so the field is dropped rather than constantized — a kind
      string nothing consumes invites the belief it is reported;
      `SceneOperationKinds.NodeCreate` is the only pre-existing kind unpinned in
      `SceneOperationKinds_AreStableStrings` and gets pinned; and the callerless
      transform log wrappers `LogCannotUpdateTransform`/`LogUpdatedTransform`/
      `LogFailedToUpdateTransform` go the way the rename wrappers did. Closed
      2026-10-05 (`34ff74859`): `Scene.Mutation` is now a constant used at the
      replay-notice site and pinned, `NodeCreate` is pinned too, the never-read
      `EditSessionToken.OperationKind` field and its plumbing are gone, and the
      three callerless transform log wrappers are deleted.
- [x] C51 — `CreateFolderAsync` still had the C45 shape for a stale
      **`parentNodeId`**: `EnsureNodeInLayout` throws after the layout was
      seeded, so the rejection leaves an uncommitted, un-undoable seed behind
      while reporting `CREATE_FOLDER_FAILED`. Same fix as C45 — validate before
      seeding — found during that change and deliberately not bundled into it.
      Closed 2026-10-05 (`8b09817c3`): the parent node is resolved before any seeding and
      rejection publishes `STALE_TARGET`; red check observed on both the wrong code and the
      left-behind seed.
- [x] C48 — quick-add and `CreatePrimitiveAsync` offered only **five of the
      ten** canonical primitive shapes: `NormalizePrimitiveKind` had no arm for
      Capsule, IcoSphere, Torus, Quad or SubdividedCube, so those fell through
      `NotSupportedException` into a `CREATE_PRIMITIVE_FAILED` result, even
      though the authoring model lists all ten as native-owned recipes
      (`scene-authoring-model.md:122-126`, `property-inspector.md:121-139`), the
      installed SDK declares all ten generators, the picker already asserted ten
      (`BuiltinCatalogTests.cs:56`) and a native integration test already had
      DataRows for all ten — so authors could _assign_ those shapes and could
      not _create_ them. Closed 2026-10-05: five switch arms (casing taken from
      the native `kDefinitions` list, `BuiltinGeometry.cpp:32-42`, since the
      managed switch is case-sensitive where the native resolver is not) plus
      five flat quick-add entries, with the "Advanced" distinction left to the
      picker's display type rather than inventing a submenu.
      `SceneDocumentCommandServiceTests.PrimitiveShapes.cs` pins each of the ten
      kinds to `asset:///Engine/Generated/BasicShapes/<Kind>` by literal URI
      (deliberate: a builder-shared assertion could not drift, this can) and
      pins the unknown kind to a published failure with no node and an empty
      undo stack; `SceneEditorQuickAddMenuTests.cs` pins the menu to the ten in
      order, which no test did before. Correcting my own framing: for primitives
      the docs and the engine were right and the editor was behind, so this is
      code catching up to docs — the "code is ahead, docs move" reading holds
      only for point/spot light _creation_; their property editing genuinely
      does not exist (`EngineComponentId` has Directional only, no editor is
      registered for point/spot, and `ComponentFiltersTests.cs:178-193` asserts
      the unavailable banner), so `documents-and-commands.md:196` must split
      creation from editing rather than being deleted.
- [x] C47 — a failed native rename publish was logged and the command reported
      success, so the author saw an edited label with no native change and no
      diagnostic. Closed 2026-10-05 (`ce3538328`) on the owner's rule that
      errors reaching the user must reach the user:
      `ISceneEngineSync.RenameNodeAsync` now returns a `SyncOutcome` from the
      same `ExecuteNodeSyncAsync` pipeline transforms use
      (not-running/faulted/world-mismatch → `SkippedNotRunning`; dispatch or
      interop errors → `Rejected`/`Failed` under new `OXE.LIVESYNC.RENAME.*`
      codes), and the command publishes any non-Accepted outcome exactly as
      geometry, camera and light edits do. Deferred renames now queue under
      `Scene.Node.Rename` instead of the generic `Scene.Mutation`, so a later
      replay failure names the operation the author performed. The deferral
      queue was verified rather than duplicated — `PublishReplayFailures`
      (`SceneEngineSync.Pending.cs:204`) already surfaced replay failures, which
      is why only the synchronous path was silent. Remaining half of the class
      is C44.
- [ ] C49 — workspace interaction persistence exists only for Hide.
      `settings-architecture.md:65` records hidden nodes; there is no typed
      storage for per-project Lock state, category/column placement or their
      lifetime, and no single service owns workspace interaction substate, so
      lock and column choices cannot survive a session. Scope is decided in D4 —
      implement one coordinated workspace service with typed settings and
      project/scene-scoped lifetime, not new fields on scene DTOs. No code
      exists yet, so this is unimplemented work, not an open question. Lock here
      means the editor-editability lock specified in the "Show in Editor & Lock"
      contract above — orthogonal to viewport suppression and to runtime
      visibility — and the persistence must key on the stable node id, so a
      renamed or reordered node keeps its state.
- [ ] C50 — clipboard lifetime is not enforced. Copy carries source ids and
      live-adapter-derived state instead of an immutable snapshot, Cut is
      scene-bound only by construction, and nothing clears the clipboard on a
      successful project switch, so a stale payload can be pasted into another
      project's scene. Scope is decided in D7 — snapshot at copy time, explicit
      invalidation on project switch, one intent path so no second history
      authority appears.
- [x] C42 — dead and unsafe public API on the command interface. **Closed
      2026-10-04 (`e4eaf2f44`)**: `RenameItemAsync` deleted from the interface
      and implementation (no callers, no test removed) together with the
      `ISceneExplorerService` constructor parameter it alone kept alive, and
      `DeleteNodesAsync`/`DeleteFolderAsync` now delegate into
      `DeleteItemsAsync` so all three entry points share one prevalidation
      order, one transaction and one undo record. Deliberate behaviour drift
      from unifying them: a stale folder id is now a `STALE_TARGET` rejection
      raised _before_ mutation instead of a `DELETE_FOLDER_FAILED` caught from
      `SceneOrganizer.RemoveFolder` with no `ValidationCode`; node-only deletes
      now restore explorer layout entries on undo; batch and single-kind deletes
      share one undo label. Red check obtained concretely — reinstating the
      duplicated bodies failed exactly the two new tests
      (`DeleteFolderAsync_WhenFolderIdIsStale_RejectsWithStaleTargetValidation`,
      `DeleteNodesAsync_Undo_RestoresGraphAndExplorerLayout`) and left the
      pre-existing delete tests passing, which is the proof the copies had
      already diverged. Suite: 279 total, 279 succeeded, 0 failed, re-measured
      by the orchestrator against binaries newer than the last source edit.
      `RenameItemAsync` (`ISceneDocumentCommandService.cs:228`, impl
      `SceneDocumentCommandService.cs:511-564`) has zero callers in src or
      tests, targets by **UI adapter type** rather than identity, trims nothing,
      skips the native rename sync that `RenameNodeAsync` performs, and captures
      the adapter in its undo closure — undo after a projection rebuild would
      target a retired adapter. `DeleteNodesAsync` (`:281`) and
      `DeleteFolderAsync` (`:289`) also have **no production callers** (only
      tests, plus the one live path `RemoveSelectedItems` → `DeleteItemsAsync`
      at `SceneExplorerViewModel.cs:168,187`, bound to the Delete accelerator
      `SceneExplorerView.xaml.cs:117` and toolbar `.xaml:130`), and their
      prevalidation has already drifted from `DeleteItemsAsync`'s.

#### Test / verification gaps (editor/controls; runtime for native rows)

- [~] T1 — one undo per batch + atomic rejection (mixed delete, multi-root
  duplication). (Multi-root duplication stale-id rejection is tested;
  mixed-delete atomicity test pending.)
- [~] T2 — preserve-world ignored-parent + exact layout/order/TRS undo/redo +
  native convergence. (Ignored-parent world preservation tested; exact-layout
  undo/native-convergence pending.)
- [x] T3 — rename reaches native; delete-undo recreates the subtree.
      (`RenameNodeAsync_WhenCommitted_RecordsSingleUndoStep` verifies the native
      rename call; `DeleteNodesAsync_Undo_RecreatesFullSubtreeInNative` verifies
      `CreateNodeAsync` twice, root + child.)
- [ ] T4 — immutable snapshots, sequential-scene Copy, repeated Copy Paste,
      Escape cancels Cut.
- [ ] T5 — folder/mixed selection reaches the right Inspector; external
      selection reconciles.
- [x] T6 — search multi-query restoration, folder ancestry, no authored
      expansion writes.
      (`SearchAsync_RestoresExpansionAcrossSuccessiveQueriesWithoutAuthoringLayout`
      covers two successive queries, accumulated expansion, restoration on
      clear, serialized-layout equality and `IsDirty`;
      `SearchAsync_RevealsNodeInsideCollapsedNestedFolders` covers nested visual
      folder ancestry.)
- [ ] T7 — context-menu captured context, keyboard/touch, stale dismissal,
      toolbar parity.
- [ ] T8 — single loaded scene: guarded replacement, material-tab retention,
      configured default.
- [ ] T9 — generic recycling detach, displayed-scope keyboard, reload/reattach,
      Escape cancel.
- [ ] T10 — 32/40 density profiles + measured 1,000-item lazy-tree workload.
- [x] T11 — D5's layout-only qualifier was untested beyond the flat root-folder
      case: folder Delete inside a nested folder, and inside a folder whose
      grouping followed a D2 reparent, must both promote entries without
      deleting nodes and without changing `SceneNode.Parent` or world pose.
      Closed 2026-10-05 (`bff041741`): inner-into-outer promotion, outer
      promoting the inner folder with its children, both through
      `DeleteFolderAsync` and the mixed batch as one undo step each, plus D2
      cross-scope cases asserting parent identity, exact local TRS and world
      matrix unchanged before and after. All four were green on first execution
      — D5 held, so these are guards and no red state was obtainable.
- [x] T12 — prove command-driven mutations reach the demand service's observers:
      add/remove a node, child or component through the owning command and
      assert `RefreshReferenceObservers` re-subscribes the new collection graph
      and obsolete demands are invalidated, with no stale demand surviving a
      rename or reparent. Replaces the `AuthoringChanged` "integration note"
      that P6 retired. Closed 2026-10-05: 8 Unit tests prove the command path
      mutates the observed collections and re-exposes created sources, green in `9d54484ea`
      (313/313), and the five service-level Unit.UI tests ran for the first time and pass
      (`df463f890`/`1996f30e7`, 14/14). Getting them to run at all exposed a latent harness
      hang: contentless demand tests awaited a composition tick that never came, because the
      shared test window is created only lazily — fixed by realizing it in the fixture, one
      line, no assertion changed, and five pre-existing contentless tests in the same class
      stopped hanging as a side effect.

- [ ] T13 — the shared inspector field finder is timing-sensitive. While proving the cascade
      fix, one of six filtered 41-case Integration.UI runs exited 5 with a single test
      failure; the wrapper does not surface the executable console, so the case could not be
      identified, and four subsequent runs were clean. Most likely the hit-test visibility
      gate in `testsupport/UI/InspectorControls.cs:101` racing layout after a scroll. This is
      recorded rather than papered over: if it recurs, stabilize it with a bounded,
      condition-based wait on the layout state actually being waited for — not by raising the
      step bound or the pump delays.

#### Progress-note corrections (reconcile §6 with the above)

- [x] P1 — §6 closure statement rewritten as an in-progress status naming
      C32/C34/C35/C38 and T9/T10. (C33/C36 had already closed, so the original
      "until C32–C36" range was too wide.)
- [x] P2 — condition met (C8/C9/C10 closed); the §6 atomic graph+layout claim is
      kept and cited.
- [x] P3 — §6 claim downgraded to "partly in place", naming C30 (no
      guard/stage/retire) and C31 (no creation seed) as the substance still
      owed.
- [x] P4 — condition met (C6/C13 closed); the §6 "every drop intent" claim is
      kept and cited to `SceneExplorerDropTests` and the D1 default.
- [x] P5 — the stale "all ten steps implemented" claim was not §6's evidence
      paragraph (which already carried the adversarial-review correction) but
      the **SE-01/SE-02 status-table rows**; SE-01 is now `in_progress` and
      SE-02's cell lists what is closed with evidence against what remains.
- [x] P6 — restated in §6 against the observed mechanism:
      `SceneContentDemandService` subscribes
      `RootNodes`/`Children`/`Components`/`OverrideSlots` and routes
      `CollectionChanged` to `RefreshReferenceObservers`, so no
      `AuthoringChanged` signal is missing; the unproven part is that
      command-driven mutations reach it, moved to T12 instead of staying an
      SE-03 note.
- [ ] P7 — `documents-and-commands.md:179-184` names five operations that do not
      exist in `ISceneDocumentCommandService` (`DeleteNodeHierarchyAsync`,
      `DeleteExplorerFolderAsync`, `CreateExplorerFolderAsync`,
      `RenameExplorerFolderAsync`, `MoveExplorerLayoutItemAsync`; grep across
      `src` returns zero hits, and `ReparentNodeAsync` is really
      `ReparentNodesAsync`). This is **not** a rename-the-doc task: read what §8
      intends to pin down, judge whether the implemented surface (identity-based
      commands, whole-batch prevalidation, one undo step, grouping-only folder
      moves vs lineage-driven reparent) is the right model for a game-engine
      editor, then update the doc to match only where the code is deemed good
      enough — where it is not, the code is the defect and needs its own owner
      decision or C-item. **Review outcome (2026-10-04, re-verified):** §8's
      real contract is single mutation authority + operation-kind coverage + the
      side-effect pipeline; the method list is explicitly illustrative ("for
      example", doc:173) and the five phantom names appear in **zero commits**
      touching `projects/` (`git log --all -S`), so this was a planned-API
      sketch that never matched anything built, not rename drift. The
      implementation is the better model for an editor on the families audited
      (identity targeting, whole-batch prevalidation, one undo per batch
      confirmed through `HistoryKeeper.cs:356-399` nesting, synchronous
      dirty/revision advance) — so the list is replaced by families + a pointer
      to the interface as authoritative, not renamed. Where code contradicts the
      _recorded_ decisions or the doc's own normative rules, code moves: that
      became C39-C42. Interface is 33 methods (+1 class-only overload), and its
      kind table is a strict subset of `SceneOperationKinds` (11 vs 19).
      Proposed §8 wording is pending the owner's approval; the one-to-one claim
      at doc:172 is false in both directions (`DeleteItemsAsync` → 2 kinds, four
      layout methods → 1 kind).
- [ ] P8 — cross-doc consistency pass: four texts still contradict the owner's
      recorded D1/D2. The interface XMLDoc says grouping happens "without
      changing scene parenting or transforms"
      (`ISceneDocumentCommandService.cs:317`); the plan's own drop table says
      "Into folder | Group only; every affected node must already have the
      folder's actual parent scope" and "first explicitly reparent… reject"
      (`:439`, `:443-445`); §6 said "Folders are grouping-only (no Parent/TRS
      change); true reparent preserves world pose" (corrected in place); and
      `scene-explorer.md:250` still lists copy/paste/duplicate as out of ED-M03.
      Trusting any of them makes owner-approved correct code look like a bug —
      which is exactly the trap this pass exists to close.

### SE-03 — Showcase UI, workspace protection and live integration

**Dependencies:** SE-01/02; actual runtime picking/editor-mask capability and
approved workspace persistence contract. **Deliverable:** the production
Explorer follows the brief/showcase, with all visible actions connected and
native behavior verified.

**Touch points:** SceneExplorer view, scoped styles and small view adapters;
WorldEditor Workspace/SceneEditor and new owning workspace-interaction service;
Editor.Data typed settings usage; Program.cs/WorldEditor composition
registrations; Runtime/Interop supported API owners if required; tests and
concise validation notes.

1. Compose the final view as header/search, picking categories, bounded tree and
   bottom command strip. Use generic row slots with Oxygen templates for count/
   eye/lock. Reuse DroidNet ToolBars/Menus and semantic icon sources; remove red
   border/random thumbnail/reflection/dialog rename. No copied giant tree
   template in WorldEditor and no manual ItemsRepeater replacing DynamicTree.
   Implement the required row/root/background menus via the existing Menus host.
   Verify context preparation order, right-click selection, Shift+F10/Menu-key/
   touch access, disabled reasons, focus restoration, stale-context dismissal
   and realized-row cleanup. Context menu and toolbar must produce identical
   command effects for equivalent contexts; text-control menus remain native.
2. Implement local hide/lock/category state in a document-aware WorldEditor
   workspace service using IEditorSettingsManager/project context, modeled on
   the existing PreviewSettingsService lifetime/persistence pattern. Follow
   settings LLD's project-ID checks, user-local scope, per-scene node sets and
   stale-write suppression. Persist no workspace flags in scene/cooked data or
   authored history.
3. Derive local/effective/inherited hide/lock and named ancestor explanations
   from true scene ancestry. Update descendant projections after reparent, undo
   or workspace change without overwriting child entries. Locked selection
   remains inspectable; propagate editing-disabled explanation through Inspector
   and tools. Use one eligibility owner at all user-authoring entry points;
   undo/redo restores committed state rather than becoming unusable merely
   because a node was later locked.
4. Apply hide to the editing main-view geometry/gizmo representation only.
   Preserve authored lighting, shadow-caster eligibility and native authored
   visibility. Restore masks after view recreation; qualification views omit
   them. Capture project/document/scene/view generation for every runtime
   application/result. Report persistence/native failures through existing
   operation/result surfaces.
5. Implement category controls through the actual viewport pick owner. Exclude
   disabled categories **before choosing the nearest eligible hit**; do not
   discard only the nearest result and miss a valid hit behind it. Hidden
   representations are not hit targets; locked nodes can still be selected for
   inspection. Keep navigation/gizmo precedence and stale-result protection.
   Explorer selection is independent of pick eligibility; category toggles never
   filter the tree.
6. Complete Inspect/reveal: clear the visible search field and underlying
   filter, expand target ancestry, realize/scroll/focus the row, select its
   document-scoped identity and requested Inspector component. Do not frame/move
   the viewport. Show all clears only hide; no accidental clearing of locks or
   child state. Verify scene/project load failures and rapid replacement against
   the shared activation generation; Explorer, Inspector and viewport never
   display different accepted scenes. Reapplying mounts/settings does not reopen
   the default scene.
7. Exercise the showcase journeys against native controls using reproducible
   scene data with both logical folders and a real parented spotlight. Add
   durable design-target geometry/state/interaction tests and
   performance/workload checks. Validate narrow docks, long names, both resource
   density profiles, text and DPI scaling separately, Light/Dark/contrast,
   keyboard and native drag/drop.
8. Update DynamicTree generic docs, owning Explorer/settings/selection contracts
   and obsolete source-draft pointers. Record concise implementation/test/native
   behavior and design-review outcomes beside this plan; link a summary from
   IMPLEMENTATION_STATUS.md without rewriting historical milestone delivery.

**Acceptance:** every section 4 behavior is exercised and production-connected;
the native Explorer matches the design's hierarchy, proportions and interaction
intent; Show all/Hide/Lock/picking affect only their declared scopes;
project/scene/ view switches retain the correct workspace state; actual native
mask/pick behavior and hierarchy convergence are observed. A disabled “not
implemented” eye/picking button or fake acknowledgment does not qualify full
completion.

## 7. Design-target verification, not old-UI fidelity

Create durable tests around the following scenarios. Assertions must prove
visible behavior, not “set IsExpanded then read IsExpanded” or a private helper
call.

| Target journey                          | Required proof                                                                                                                                                                                                                                                                   | Owner/slice                                                 |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| Spotlight search                        | Collapsed Pavilion spotlight appears with ancestors; unrelated rows are excluded; match count correct; selected Sculpture is not silently replaced; clear restores expansion.                                                                                                    | Explorer Unit/UI, SE-02/03                                  |
| Picking versus search                   | Toggle Mesh off: rows/geometry remain; Explorer can select it; native pick chooses nearest enabled-category hit. Categories survive searches and reflect scene contents.                                                                                                         | Workspace Unit + native integration, SE-03                  |
| Parent hide/lock                        | Parent action disables inherited child action with explanation; child's local choice survives parent restoration; inspection remains possible; Show all changes Hide only.                                                                                                       | Workspace/Explorer/Inspector UI + native integration, SE-03 |
| Generic slot reuse                      | A non-scene “status/action” row and a default no-slot tree work; action clicks/keys do not select/drag; trailing columns align across depth and recycling.                                                                                                                       | DynamicTree Models/UI + DemoApp, SE-01                      |
| Rename                                  | F2/toolbar/menu use same inline editor; Enter commits once; Escape rejects draft; invalid/stale target no mutation; text shortcuts do not delete the row.                                                                                                                        | DynamicTree UI + Explorer command/UI, SE-01/02              |
| Context-menu targeting                  | Selected-row right-click retains the full batch; unselected row becomes exclusive; background targets root without changing Inspector. Node/folder/mixed/root menus have the specified commands, no node-subset mutation and consistent toolbar eligibility.                     | Explorer Unit/UI, SE-02/03                                  |
| Context-menu accessibility/lifetime     | Shift+F10/Menu key, pointer and touch use the same target rules; text context stays native; Escape closes only menu; focus returns or advances after Delete; clipboard/lock updates do not retarget; scene switch/row recycling closes stale menus; unload/reload can reopen.    | DynamicTree/Menus UI + Explorer Unit/UI, SE-01/03           |
| Filtered selection                      | Ctrl nodes/folders, displayed Shift range, root exclusive, primary identity, folder/mixed summary; default generic unfiltered SelectAll still passes.                                                                                                                            | DynamicTree + selection/Inspector tests, SE-01/02           |
| Drop/paste                              | Before/Into/After cues agree with destination and actual saved order; ancestor+child moves once; invalid lock/cycle/scope rejected; failed async result cannot mutate another document.                                                                                          | DynamicTree UI + Explorer commands/integration, SE-01/02    |
| Drag cancellation and Copy              | Ctrl-drop preserves the user's clipboard; Escape/outside/stale drop has no mutation; edge scroll/600-ms expansion stop; DropCompleted cannot apply a second mutation.                                                                                                            | DynamicTree UI + Explorer command/UI, SE-01/02              |
| Configured default and one loaded scene | Serialized default survives project reopen and scene rename; explicit load does not rewrite it; last-opened/discovery order never override it; invalid default is visible; material tabs/context refreshes do not switch scenes.                                                 | Projects + document/workspace Unit/UI/integration, SE-02/03 |
| Sequential-scene clipboard              | Copy A, unload A, load B, Paste creates new identities/world poses using snapshot; only B history changes. Cut A is cancelled on successful switch; cancelled switch retains it; successful project switch clears only Oxygen clipboard.                                         | Explorer clipboard + document/workspace tests, SE-02/03     |
| Paste intent and folder safety          | Normal node Paste is sibling; explicit Paste Into is child; ambiguous multi-scope target rejected; repeated Copy Paste works; external cross-scene node refs reject. Folder Delete promotes entries; node Delete deletes hierarchy; copied lights do not steal atmosphere roles. | Explorer command/UI and Save/reopen, SE-02/03               |
| Grouping versus parenting               | Folder organization changes no Parent/TRS; true reparent preserves world pose under rotated/scaled parents; singular/shear rejection is all-or-nothing; undo/reopen exact.                                                                                                       | Mutator/Organizer/command + native integration, SE-02/03    |
| Clipboard                               | Cut does not delete; cancel restores appearance; Paste is one operation; Copy creates independent hierarchy/components/IDs; repeated paste and role-reference policy valid.                                                                                                      | Generic clipboard + Explorer command/UI, SE-01/02           |
| Persistence/lifetime                    | Authored commands dirty/save/sync; search/selection/picking/hide/lock do not dirty or enter scene undo; reopen/switch/project mismatch/stale callbacks behave correctly.                                                                                                         | Document/workspace Unit + Integration.UI, SE-02/03          |
| Layout/accessibility                    | 280/360/540-DIP docks, short/tall heights, long names, text 100/150/200%, rasterization 1/1.5/2 separately, Light/Dark/contrast; aligned state columns, visible focus, reachable overflow, no text clipping.                                                                     | Native UI tests + reviewer inspection, SE-01/03             |
| Workload                                | At least the PRD's 100-node scene and a 1,000-item generic lazy tree: bounded realized rows, no unsolicited lazy loads on generic filtering, no whole-tree rebuild per value change, no expanding observer counts after reload.                                                  | DynamicTree/Explorer measured tests, SE-01/03               |

Use actual render waits and real header/automation/native input for interaction
tests. Test within the tree's bounded ScrollViewer; do not wrap it in another
unconstrained scroller or treat virtualization as proven by a small example.

Visual review compares the **new native UI to the brief and showcase**, never to
old Explorer screenshots. Use optional local-only screenshots of the target
showcase and new native implementation to explain differences. No committed
golden images, screenshot matrix infrastructure, preservation baseline or
pre-change capture requirement. Reviewer evaluates design fidelity and
meaningful WinUI adaptations; a complete screenshot collection is not a
prerequisite for coding. Native functional tests and genuine runtime
observations are mandatory. Do not claim final appearance/usability approval
unless the reviewer performed it.

## 8. Build/test execution and completion

Commands run from repository-root PowerShell in a VS developer shell. Build each
owning project before running its tests; use the pinned root toolchain and
installed native SDK. Avoid concurrent builds sharing outputs.

```powershell
MSBuild.exe projects/Oxygen.Editor.Projects/tests/Oxygen.Editor.Projects.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.Projects/tests --configuration Debug

MSBuild.exe projects/Controls/DynamicTree/tests/Models/Controls.DynamicTree.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Controls/DynamicTree/tests/Models --configuration Debug

MSBuild.exe projects/Controls/DynamicTree/tests/UI/Controls.DynamicTree.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Controls/DynamicTree/tests/UI --configuration Debug

MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Unit/Oxygen.Editor.WorldEditor.Unit.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit --configuration Debug

MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Unit.UI/Oxygen.Editor.WorldEditor.Unit.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit.UI --configuration Debug

MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Integration.UI/Oxygen.Editor.WorldEditor.Integration.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Integration.UI --configuration Debug -- --filter FullyQualifiedName~SceneExplorer

MSBuild.exe projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
MSBuild.exe projects/Controls/DynamicTree/src/Controls.DynamicTree.csproj /m /p:Configuration=Debug /p:Platform=x64 /p:RunAnalyzersDuringBuild=true
MSBuild.exe projects/Oxygen.Editor.WorldEditor/src/Oxygen.Editor.WorldEditor.csproj /m /p:Configuration=Debug /p:Platform=x64 /p:RunAnalyzersDuringBuild=true
```

The SceneExplorer Integration.UI namespace/suite is to be added; the
corresponding filter must discover actual tests, not report zero tests as
success. Run affected existing Inspector/SceneEditor/document lifetime
integration cases as well. Reuse the existing test projects/WinUI host and
SceneAuthoringFixture; do not create a competing harness. Verify Controls
DemoApp's consuming build when public APIs change. Run changed-file hooks and
`git diff --check`; report analyzer results separately from compiler and hook
success.

If a shared Menus lifecycle/context-preparation/input API needs correction,
build and run its owning suite, including ContextMenuTests/PopupMenuHostTests,
as well as the new Explorer-level context-action tests. Passing a mocked host
test alone does not prove native keyboard placement, focus restoration or
stale-row behavior.

```powershell
MSBuild.exe projects/Controls/Menus/tests/Controls.Menus.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Controls/Menus/tests --configuration Debug
```

For native additions follow [engine
instructions](../../../projects/Oxygen.Engine/AGENTS.md) and the owning
runtime/interop workflow only after authorization. Do not guess a CMake target
or run a whole engine build from this managed checklist. Record native
dependency/build/test ownership before implementation and keep SE-03's native
acceptance blocked until the required API and observed behavior exist.

| ID    | State                   | Next action                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | Responsible role                       |
| ----- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------- |
| SE-01 | in_progress             | Close C32/C34/C35/C38 and T9/T10, then seek demo acceptance (P1).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | Implementer + user                     |
| SE-02 | landed_needs_validation | Corrected per P5: the steps landed, the slice is not validated. Closed with evidence 2026-10-04: transforms/reparent, atomicity (C8-C10), clipboard (C11-C15), selection-adjacent drop routing (C6/C13), search and layout domain (C24-C27, T6) — 277/277 unit tests. Still open: selection C16-C19 and context menu C20-C23 (gated on D3), scene replacement C30, creation seed C31, reconcile-during-search C37, gaps T4/T5/T7/T8/T12, approvals D3-D8. The `AuthoringChanged` item is not deferred to SE-03 — see the P6 restatement in §6 and T12. | Implementer                            |
| SE-03 | planned                 | Connect target UI, workspace state and supported native mask/picking; qualify workflows.                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Implementer + runtime owner + reviewer |

Use `in_progress`, `landed_needs_validation`, `blocked`, `validated` accurately.
Document contract decisions and named dependencies next to the slice, not as a
new sequence of small milestones. Keep one concise outcome/evidence/remaining
record per slice, not a session diary. No automatic commits.

**Full completion:** all three slices' exits met; DynamicTree proven usable
without Oxygen; every target action production-connected; correct
command/workspace scopes; save/undo/runtime and stale-lifetime tests passing;
new native design reviewed; owning contracts/docs current; no temporary
captures/scripts or duplicate legacy paths in the diff. State any unrun checks,
native blockers or reviewer sign-off still outstanding instead of declaring
completion from a build or mocked tests.
