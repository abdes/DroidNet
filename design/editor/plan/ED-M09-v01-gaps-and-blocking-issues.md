# ED-M09 - V0.1 Gaps And Blocking Issues

Status: `planned`

## Completion Ledger

One row per item that needs work, in the order of work. The issues keep the
owner's order from section 3; the LLD gaps of section 2 start once their
dependencies are closed. An item is `done` only when the done gate of section 6
holds; its row then names the commit.

**Next: #77.** It is first because it decides where a node's visibility and
shadow choices live, so the Inherit source modes cannot be designed before it.
Its first step is a design proposal per unused part (wire end to end or
remove), argued from user scenarios and the engine's capabilities; the proposal
goes to the owner before any code changes.

| Order | Item                                              | Needs         | State | Commit |
| ----- | ------------------------------------------------- | ------------- | ----- | ------ |
| 1     | #77 fate of unused override slots and scopes      | -             | open  | -      |
| 2     | #94 session services own their project scope      | -             | open  | -      |
| 3     | #95 Scene Explorer history without `UndoRedo`     | -             | open  | -      |
| 4     | #74 one view per view model per host              | -             | open  | -      |
| 5     | #76 LivePreview leaves Ready on engine fault      | -             | open  | -      |
| 6     | Inherit source modes (section 2)                  | #77, #94, #95 | open  | -      |
| 7     | Light collection by resolved visibility           | 6             | open  | -      |
| 8     | Hide mask step 1: engine hidden-root snapshot     | 7             | open  | -      |
| 9     | Hide mask step 2: Interop editing-view mask       | 8             | open  | -      |
| 10    | Hide mask step 3: Runtime port                    | 9             | open  | -      |
| 11    | Hide mask step 4: editor dispatch and checks      | #76, 10       | open  | -      |
| 12    | Material emission editing                         | #95, #76      | open  | -      |
| 13    | #81 producer identity of same-named sources       | -             | open  | -      |
| 14    | #97 moved output group retires old cooked outputs | -             | open  | -      |
| 15    | #86 consistent editing commands across parts      | -             | open  | -      |
| 16    | #96 one file access model                         | -             | open  | -      |
| 17    | #93 badge text in `AssetStatusText`               | -             | open  | -      |
| 18    | #73 Content Browser keyboard focus                | #86           | open  | -      |
| 19    | #72 Content Explorer tree context menu            | #86           | open  | -      |
| 20    | #87 texture cooker formats and settings           | -             | open  | -      |

Ordering decisions:

- Items 7 and 8 change engine scene traversal in order: light collection first
  reads the visibility that item 6 resolves, then the Hide mask skips subtrees
  in the scene preparation path, so each engine change is tested against the
  final resolution rule.
- The LLD gaps start after #94, as section 3 requires, and after #76, because
  the Hide mask checks and the emission preview need a LivePreview that
  reports engine faults.

## 1. Purpose

Close what V0.1 still lacks before input authoring and release qualification:
the LLD behaviors that are designed but not built, and the abdes/DroidNet issues
that must be fixed before V0.1.

## 2. LLD Gaps

The design pack audit found three LLD behaviors that are not built.

| Gap                       | LLD                                                                                                          | Today                                                                                                                                                                                     | Work                                                                                                                                                                                                     |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Inherit source modes      | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state), inspector 4 | World stores `IsVisible`, `CastsShadows` and `ReceivesShadows` as booleans, so the editor authors local values only; the Rendering section offers no Inherit and shows no resolved origin | Inherit / Shown / Hidden and Inherit / On / Off in World source and `SceneNodeData`, Scenes descriptors, Interop `NodePropertyApplier` and the cooked node records; migrate the booleans as local values |
| Light collection pruning  | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state)              | `DirectionalLightResolver` and `SceneRenderer::BuildFrameLightSelection` traverse with `VisibleFilter`, so a locally Shown light under a Hidden parent contributes nothing                | Collect lights by each node's resolved visibility, as geometry extraction does; keep `VisibleFilter` for callers that prune subtrees deliberately                                                        |
| Material emission editing | [Material editor](../lld/material-editor.md)                                                                 | `MaterialSource` carries and writes `EmissiveColor` and `EmissiveIntensity`; `MaterialEdits` has no emission edit and the Material Editor shows emission only as a texture slot           | Emission colour and HDR intensity edits in `Oxygen.Editor.Materials` and their Material Editor controls, with history, Save, cook and preview                                                            |

**Inherit source modes** means this. A node has three visual-state choices:
Scene Visibility, Geometry Cast Shadows and Geometry Receive Shadows. Today each
is one boolean, so the user can only say "this node is shown" or "hidden". The
scene model says each is a source mode: **Inherit** (take the parent's resolved
value) or an explicit local value (Shown / Hidden, On / Off). A new root starts
Shown / On / On, a new child starts Inherit, and an Inherit root resolves to
Shown / On / On. Hiding a parent then hides every child that inherits, while a
child set explicitly to Shown stays active. The inspector's Rendering section
shows the mode and the resolved value with its origin. The work changes the
stored value in five places (World source and `SceneNodeData`, the Scenes
descriptors, the Interop `NodePropertyApplier`, the cooked node records and the
inspector) and migrates the existing booleans to local values by editing the
data. It is blocked on #77, which decides whether these choices stay node
fields or move into override slots; the modes are added to whichever mechanism
#77 keeps.

The editor Hide engine mask
([settings](../lld/settings-architecture.md#51-workspace-visibility-storage-and-lifetime))
is not built: the eye keeps state, persistence, undo and picking exclusion, and
logs that viewport suppression is pending. The work, in order:

1. Engine: a scene-scoped hidden-root snapshot with revision in
   `ScenePrepContext`; `ScenePrepPipeline::PrepareView` skips the descendant
   closure, emitting no render items and no shadow casters for editing views;
   Vortex design note. Test: a hidden mesh has no draws and no casters in the
   masked view and renders in an unmasked one.
2. Interop: `EditorModule::SetEditingViewHiddenNodes(revision, ids)` queued with
   world commands, IDs resolved through `NodeRegistry` into the snapshot of the
   scene's editing views in `OnPublishViews`, unresolved IDs retained;
   `OxygenWorld::SetEditingViewHiddenNodes`.
3. Runtime: `RuntimeSetEditingViewHiddenNodes` and
   `IRuntimeWorldCommands.SupportsEditingViewMask`.
4. Editor: dispatch the mask on change; check hide of a mesh and of a parent
   (geometry and shadows gone in every editing pane, kept in game and capture
   views), show of a parent keeping the child hidden, and the gizmo on a
   selected hidden node.

## 3. Blocking Issues

The owner selected these abdes/DroidNet issues; each must be closed before
V0.1. They are listed in the order of work.

| Order | Issue | Why it blocks V0.1                                                                                                                                                                                           |
| ----- | ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1     | #77   | Decides where a node's visibility and shadow choices live (the node's own fields or the `RenderingSlot` and `LightingSlot` override slots); the Inherit source modes of section 2 are built on that decision |
| 2     | #94   | Session services take a `ProjectContext` per call; settle project and scope ownership before the new work                                                                                                    |
| 3     | #95   | Scene Explorer history falls back to the static `UndoRedo.Default`; the Inherit edits need correct history                                                                                                   |
| 4     | #74   | Views rebuilt over long-lived view models make the editor unstable                                                                                                                                           |
| 5     | #76   | LivePreview stays Ready after the engine faults or stops; the Hide mask and emission preview need it reliable                                                                                                |
| 6     | #81   | Sources with the same file name in different folders look like one producer, corrupting cook data                                                                                                            |
| 7     | #97   | Moving a model's output group must retire its old cooked outputs, through save, reopen and cook                                                                                                              |
| 8     | #86   | The editing commands must behave the same across parts, as Windows does; adds the context service that #91, #90 and #92 read                                                                                 |
| 9     | #96   | One file access model (`IFileSystem`, `DroidNet.Storage`, `System.IO` statics)                                                                                                                               |
| 10    | #93   | Move `AssetEntry` badge text into `AssetStatusText`                                                                                                                                                          |
| 11    | #73   | Content Browser keyboard focus across the tree and the asset list                                                                                                                                            |
| 12    | #72   | Content Explorer tree context menu that adapts to the node type                                                                                                                                              |
| 13    | #87   | Cooker BC4, BC5 and BC6H, maximum size, alpha handling and channel packing for textures                                                                                                                      |

Dependencies between the issues:

- The Inherit source modes (section 2) need #77's decision first.
- #73 (focus) and #72 (tree context menu) build on #86: #86 delivers the
  context service and the `Edit.*` commands that the context menu entries and
  the focus rules run through.
- #86 does not need #91 (activity, typed clipboard and drag, context-menu
  contributions, per-window dialogs), which stays after V0.1.

## 4. Rules For Every Change In This Milestone

These are the lessons of the asset identity and scope topology work. Each one
names a way earlier changes created the debt they were meant to remove. Read
the ones that match a change before its first edit; the per-issue notes in
section 5 point back here.

### 4.1 Scope and decisions

- A problem found while working an issue is fixed in the same change. Nothing is
  deferred, parked behind a pending or allowlist entry, or recorded as "waits
  for" without the owner's approval; an approved deferral is a GitHub issue on
  abdes/DroidNet with **Observed** (each fact marked confirmed or unconfirmed,
  with files and lines), **Design**, **Recommended course of action** and
  **Done when**.
- V0.1 exposes what the engine can do now. Code or docs that exclude or force
  off an engine capability are defects to raise, not scope.
- A missing seam or a bad API is never worked around (casting to concrete
  types, reading telemetry as control, time-based guesses, interim paths). Stop,
  describe the gap, propose the refactor (owner, API shape, cost) and wait.
- A design question is answered from user scenarios and the engine's actual
  behavior (one rendered scene, one scene document open at a time, AssetLoader
  caches within budget), never from today's code shape. The new contract drives
  the code, not the reverse.

### 4.2 Reuse before building

Check the library first, in designs and plans as well as in code, and name the
reused piece in the change.

| Need                                                 | Read and extend                                                                                             |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Windows, documents, tabs, dialogs, menus, decoration | `projects/Aura/src`, its READMEs and `Aura/samples/MultiWindow`; add a missing hook to Aura                 |
| Inspector rows, numbers, vectors, colors, trees      | `projects/Controls/*`, `Oxygen.Editor.Controls`, Inspector controls; add the missing feature with its tests |
| Coordinate spaces and DPI                            | `projects/Coordinates`; never multiply by `RasterizationScale` by hand                                      |
| Uploading, staging, versioning, recycling GPU data   | `src/Oxygen/Vortex/Upload/` and `src/Oxygen/Nexus/`; reusable additions go in the owning module             |
| Test fakes of a module's ports                       | the module's `testing/<Module>.Testing` kit                                                                 |

Inspector rows use icon buttons with descriptive tooltips, not text hyperlinks.

### 4.3 No new debt

- One owner per identity string. A mount name, extension, owner prefix, group
  name or schema key gets one constant or API before its first call site; a
  caller never spells it twice.
- A value that gets a better type changes the existing property's type; never
  add a second property that holds the same value.
- Migrate data by editing the data (descriptors, retained records, project
  files). Never write backward-compatibility code for an old format. A database
  schema rename edits `InitialCreate`, its designer and the snapshot as if the
  new name had always existed, with no rename migration; name the table and
  column in the report so the owner migrates their database by hand.
- The owner clears and recooks their own projects (F1 Validation, Test Scene
  Rename, Vortex); never edit them. Example cooks go directly into the in-tree
  `Examples/Content/.cooked`.
- No silent existence checks standing in for a contract, no "kept until later"
  leftovers, no test-only public API, no dead code, no undisclosed deviation
  from the agreed design.
- Visibility errors after a move are ownership questions. Never add
  `InternalsVisibleTo` or widen `internal` to `public` to compile; audit the
  member and its outside consumers first. `Oxygen.Managed.Core` is shared with
  the engine: no editor concepts, and ask before adding types to a shared
  assembly.
- Scene edits are serializable id/value records with typed ids and explicit
  targets, callable from any thread ([scene edits](../lld/scene-edits.md)), so
  remote control stays a thin adapter.
- Show in Editor (eye), Lock and runtime visibility are three independent
  states ([settings](../lld/settings-architecture.md)); state the distinction in
  anything that touches them.
- Window lifecycle: closing the Project Explorer exits; closing the workspace
  window closes the project and all its windows and returns to the Project
  Explorer, never exits; one scene editor tab at a time.

### 4.4 Code conventions

- C#: write `.editorconfig`-compliant code from the first edit (`this.`
  qualification; statics before instance, nested types last). Split methods
  that trip MA0051 into real private methods; local functions do not count. Fix
  every warning in files you touch, whatever its origin; fix none in untouched
  files.
- Logging, in every C# file new or touched (read one `*.Logs.cs` in
  `projects/Aura/src` first):
  - every `[LoggerMessage]` is in `<Type>.Logs.cs` as a multi-line attribute
    with `SkipEnabledCheck = true`, paired with a wrapper that takes raw domain
    data, checks `IsEnabled` first and builds locals after the guard;
  - never pass an invocation, `Join`, `ToString` or LINQ into the generated
    call, and never build a log argument at a call site;
  - Debug and Trace wrappers carry `[Conditional("DEBUG")]`; types take
    `ILoggerFactory? loggerFactory = null`.
- Anything that moves, rewrites or deletes user files, or closes or reopens
  documents, logs from its first implementation: Information per committed
  action with paths, Warning for rejections and skipped work, Error for failures
  and rollbacks, Debug for plan details. Never swallow a background failure
  unlogged.
- C++ (engine): trailing return types, checked optionals, trailing commas,
  `observer_ptr`, `_NDAPI`, C++20 headers; follow the
  [Oxygen engineering rules](../../oxygen/RULES.md). `NodeHandle` is used by the
  C++/CLI interop, so search `.h` and `.cpp` before calling a managed type
  unused.
- Diagnostics: reuse `Oxygen.Managed.Core.Diagnostics` (`DiagnosticCodes`,
  `OXE.*` and per-area codes) and the
  [operation results](../lld/diagnostics-operation-results.md) contract. A
  refusal without a code is a contract gap to raise, not to paper over.

### 4.5 Tests

- Derive every expected value from the written contract and quote the rule. A
  test that pins wrong output is part of the bug: rewrite it and fix the
  producer, never add a converter at the consumer.
- Assert contract codes and ids, never message text.
- Fixtures live in the module's own `tests/TestSupport/`. Other modules fake its
  public ports or use the kit fake; never link or copy another module's
  fixtures. A test of another module's logic moves to that module.
- Moq only for a port with no kit fake, for a few canned answers, a thrown
  failure or a call check; a port mocked repeatedly with the same setups gets a
  kit fake in its owner. Never mock models or the code under test.
- Projects come from `TestProject` on Testably `MockFileSystem`. Test project
  names end in `.Tests` (WinUI `.UI.Tests`, integration `.Integration.UI.Tests`)
  and assemblies are named `DroidNet.<ProjectName>`.
- UI tests never drive the system mouse or keyboard or need window focus; they
  use control APIs, view-model state, raised routed events and bindings. No
  flaky timing or input approximations.
- Integration tests share one native engine per process (`NativeSceneFixture`);
  batch shapes into one scene, no stress loops.
- A bug fix starts with a deterministic reproduction of the real mechanism where
  one exists.
- Before handing a change over, sweep the whole repo for every remaining
  instance of what it fixes (every mock of a port that now has a kit fake,
  every other `UndoRedo.Default` use) and fix them in the same change.

### 4.6 Build, run and verify

- Managed builds use the Insiders x64 developer shell and the `amd64` MSBuild
  through PowerShell, Debug configuration; the Integration.UI projects of
  WorldEditor and LivePreview build and run in Release against the installed
  `out/install/Release` SDK. Build only the owning test project, never
  `Oxygen.Editor.App`. Batch all edits of a pass, then one build; never edit
  while a build runs.
- Run only the test cases the change touches, with a filter. A filtered run that
  did not start is a failure. WinUI lanes get at most one targeted pass; on
  failure, stop and hand the owner the exact build and `traverse Invoke-Tests`
  commands. Never import Sponza-class scenes.
- Engine changes (#81, #87 and the light pruning and Hide mask work) follow the
  [engine instructions](../../../projects/Oxygen.Engine/AGENTS.md): explicit test
  executables with `--gtest_filter`, no whole-suite reruns, no GPU
  device-removal tests (the owner runs those).
- Never run clang-tidy, oxytidy or analyzer builds unasked. Format with
  `oxyformat` on explicit files, never directories.
- A failing test or editor bug is fixed whoever caused it. Reproduce from the
  live evidence (dump, stack, saved state) with at most one instrumented run
  that carries every useful datum; probes are announced, kept out of commits and
  removed.

### 4.7 Documents and commits

- LLDs state the design in present tense, including unbuilt parts; done and
  not-done live only in plans. A change that alters behavior updates its LLD in
  the same commit; a replaced document is deleted or rewritten, with every
  reference fixed. Never cite run artifacts or write evidence narration.
- Markdown is reflowed to 80 columns; run `pre-commit run --files <file>` until
  it reports Passed. Render-check any new Mermaid block.
- Nothing that changes what the editor shows (XAML, menus, focus visuals,
  notices) is committed before the owner has seen it in the running editor. A
  new design or plan text stays uncommitted until the owner approves it.
- Commit every modified file in the tree, including small edits you did not
  make. Run `git diff --cached --stat` first; the message describes the code
  outcome with a code-area scope, carries `Fixes #<n>`, and never cites
  milestones, slices or batches.

## 5. Notes Per Issue

Each note lists what to read first and the traps specific to the issue.

| Issue | Read first                                                                                                                                                                 | Traps and requirements                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #77   | [Scene model](../lld/scene-authoring-model.md) (visibility, participation and shadow source state), the issue's table of unused slots                                      | The decision settles where a node's visibility and shadow choices live, which the Inherit source modes of section 2 then build on. `RenderingSlot` (visibility) and `LightingSlot` (shadow casting and receiving) overlap the node's `IsVisible`, `CastsShadows` and `ReceivesShadows`: after this issue exactly one mechanism carries them. Remove what is dropped and edit existing descriptors and projects; no reader of the old shape. Search `SceneAssetReferences`, `SceneReferenceRetargeting`, the toolbar and the Interop for users of each removed type. Record the decision in the scene model LLD and update the World README. |
| #94   | [Scope topology](../lld/scope-topology.md) decisions 5, 7 and 8, the topology test                                                                                         | The services take `IProjectSession` and read `Context` when they act; a mount change keeps the project id and root, so settings keys do not change. Keep the per-scene generation checks. Sweep every other session-scoped service for a `ProjectContext` parameter or an `activeProject` field, not only the three named. A test that switched projects on one instance becomes a test with two sessions.                                                                                                                                                                                                                                  |
| #95   | [Editor documents](../lld/editor-documents.md), scope topology 5.10                                                                                                        | Verify first (the issue lists two unconfirmed effects) and report each as confirmed or not. Remove every `UndoRedo.Default` and `UndoRedo.GetHistory` use in the editor, not only the one in `SceneExplorerViewModel`. Without a document the explorer records nothing and Undo is disabled. Record changes as scene-edit records.                                                                                                                                                                                                                                                                                                          |
| #74   | Docking, Mvvm and Routing.WinUI READMEs, commit 5d6ea4532                                                                                                                  | Rule: one view per view-model instance per host. Confirm the trigger first with logged constructors before changing the hosts. Part 2 (the `DynamicTreeViewModel` multi-view contract) is a design decision to settle and record, then remove the workarounds from `DynamicTree`. Never change a XAML property from `Unloaded`: a closing window raises it during teardown. These are shared libraries; no editor concepts in them. UI tests use control APIs.                                                                                                                                                                              |
| #76   | [Live preview](../lld/live-preview.md) section 5, [operation results](../lld/diagnostics-operation-results.md)                                                             | A fault after `Ready` becomes `EngineUnavailable` with a diagnostic code and a published operation result, and `RetryAsync` restarts and remounts. Tests drive `IEngineService.StateChanged` through a fake engine service; no real device-removal test. Update the LLD, which does not cover this case. Log Warning or Error on the transition.                                                                                                                                                                                                                                                                                            |
| #81   | [Content pipeline](../lld/content-pipeline.md), `ImportManifest.cpp`, `LooseCookedIndexFormat.h`                                                                           | Remove `ImportRequest::SourceVirtualPath()` entirely and compute the producer where the manifest loads each job. The index version stays 6 but the field's content changes, so update every doc comment that describes it; the owner recooks. Tests assert `import.identity_collision` by code. Example manifests cook into the in-tree `Examples/Content/.cooked`.                                                                                                                                                                                                                                                                         |
| #97   | Content pipeline LLD (retired products), `CookRootRepair`, `IAssetRelocationService`                                                                                       | Write the integration test first and let it decide whether the old entries remain. The group a model's outputs live in has one owning API; do not rebuild the path by hand. Removal goes through `remove-cooked` and logs each removed output at Information with its path. Restore the clause in the LLD once verified or implemented. Use a small model, never a Sponza-class scene.                                                                                                                                                                                                                                                      |
| #86   | [Editor shell](../lld/editor-shell.md) 5.5 and 5.6, the issue's list of what is done, `CommandKeyBindings`, `CommandService`, the parts' `ICommandHandler` implementations | The routing, `DynamicTree`'s release of the clipboard keys and the Scene Explorer's and asset list's handlers exist; do not rebuild them. The issue's scope is: the context service (replacing the private `WorkTracker`), `Edit.Rename`, `Edit.Duplicate` and the Delete gesture, the sources tree's and the scene viewport's handling, removal of the view-local accelerators and the active part's visual. Typed clipboard and drag payloads belong to #91; focus after a content change belongs to #73. Routing tests for the asset list, the tree, the Scene Explorer and the scene editor use no system input.                        |
| #96   | `PROJECT-LAYOUT.md` section 13.3, Storage and Projects READMEs, `IAtomicFileStore`                                                                                         | Audit and classify before changing anything: `rg` also matches `fileSystem.File.*`, so the static count is unestablished. Real-disk boundaries shared with native tools are classified and documented, not wrapped. The decision is recorded in `PROJECT-LAYOUT.md`. Module tests move to `MockFileSystem` through `TestProject`. Moves and rewrites log from the first implementation.                                                                                                                                                                                                                                                     |
| #93   | [Assets module](../lld/assets-module.md) section 4, [Content Explorer identity](../lld/content-explorer-asset-identity.md) 7.2                                             | Move the members, switch every reader the issue lists (including the explorer's UI test support) and delete the members from `AssetEntry`; no forwarding. Status wording keeps one owner (`AssetStatusText`). Tests assert through `AssetStatusText`. Update both LLDs.                                                                                                                                                                                                                                                                                                                                                                     |
| #73   | Issue #86's routing, `DynamicTree` focus support, asset layout code                                                                                                        | Lands with or after #86 and must not duplicate its routing. Reuse `DynamicTree` focus support; no key handling in views; the tab order is fixed in one place. Tests use view models and control hooks only.                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| #72   | `DroidNet.Controls.Menus`, Scene Explorer's tree menu, `IAssetRelocationWorkflow`, the cook commands                                                                       | Build it the way Scene Explorer does: a menu source the view model builds per requested row. Node kinds come from one mount-kind descriptor, never from literal mount names ("Content", "Cooked", "Examples"). Actions call the asset list's workflows. A disabled entry says why. It opens from the keyboard too.                                                                                                                                                                                                                                                                                                                          |
| #87   | [Texture editor](../lld/texture-editor.md), `TextureCooker.cpp`, the descriptor schema, [Oxygen rules](../../oxygen/RULES.md)                                              | Each setting needs its schema key (one owner), request-builder mapping, cooker implementation and native tests. BC4, BC5 and BC6H first. The Texture Editor shows new settings through `inspect-texture` with no editor change; do not add editor code for them. Native tests by explicit executable and filter.                                                                                                                                                                                                                                                                                                                            |

### 5.1 Notes for the LLD gaps in section 2

- **Inherit source modes:** one enum per concept with one owner, shared by World,
  Scenes, Interop and the cook. Migrate the stored booleans as local values by
  editing the data. The inspector shows the resolved origin using the existing
  inspector controls; add what is missing to them. Blocked on #77.
- **Light collection pruning:** engine change with explicit native tests; keep
  `VisibleFilter` for callers that prune on purpose.
- **Material emission:** reuse `InspectorRgbField` for the colour and
  `InspectorNumberField` for HDR intensity, adding any missing capability to
  those controls with their own tests; edits are scene-edit style records with
  history, and Save, cook and preview all carry them.
- **Hide mask:** it affects editing views only and is independent of Lock and of
  runtime visibility. Before adding the snapshot publication in step 1, read
  `Vortex/Upload` and `Nexus` for an existing versioned-publication mechanism.
  Gaps in `IRuntimeWorldCommands` are closed at the port, not by casting to a
  concrete runtime type.

## 6. Done Gate For Each Change

A change in this milestone is ready only when all of these hold, and the report
says which it checked:

1. The contract (LLD section or issue "Done when") is met, and the LLD states the
   behavior in present tense in the same commit.
2. Debt review of the diff: no duplicated helper or idiom, no second owner of an
   identity string, no second property for one value, no dead code, no
   compatibility reader, no invented convention, no coupling to another
   module's internals, no new build-time dependency, no stale document.
3. A repo-wide sweep found no remaining instance of what the change fixes.
4. Logging inventory over every `.Logs.cs` and every `[LoggerMessage]` file in
   the touched modules, not only the new lines.
5. No warnings left in touched files; no failing test left behind.
6. Tests assert the contract by code, use the module's own fixtures and kits,
   and the filtered run passed (or the owner's lane was handed the exact
   commands).
7. The staged diff was reviewed, hooks passed, and the commit message follows
   section 4.7.

Each blocking issue is closed by a commit that carries its `Fixes #<n>` footer.
