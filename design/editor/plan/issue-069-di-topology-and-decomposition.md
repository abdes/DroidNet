# Issue #69: DI scope topology and WorldEditor decomposition

Status: `in progress` — Phase 0 and S1.1 to S1.11 (Assets, Cook on change,
Foreground cooks, Global history, Pipeline workflows out of UI, Document kinds,
Scenes, LivePreview, Asset family modules, Cook on change over checked inputs,
Shell, Unsaved documents) are done and S1.12 (Features) is in progress; see the ledger in section 6. Phase 1 follows
[editor-module-architecture.md](../lld/editor-module-architecture.md) (the
"architecture" below) and [editor-shell.md](../review/editor-shell.md) (the
"shell design"), with documents, histories, saving and cooking in
[editor-documents.md](../lld/editor-documents.md) (the "documents design"). They
replace section 7 of the spec: there are no `Authoring` or
`Authoring.Abstractions` projects, `Oxygen.Editor.WorldEditor` is dissolved, and
the Shell is the application frame.

Last Updated: 2026-10-21

Issue: [#69](https://github.com/abdes/DroidNet/issues/69)

Design owner: [di-container-topology.md](../review/di-container-topology.md)
(the "spec" below). This plan sequences the spec's phases into commit-sized
slices, records what changed in the code since the spec was written, and states
the decisions the spec left to implementation. It does not restate the target
design.

## 1. Code drift since the spec

The spec was measured at `da292a237`; 36 commits have landed since. Verified
against the current tree:

| Spec claim                                     | Current tree                                                                                                                                                                                                            | Effect on the plan                                              |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| Folder inventory (F12)                         | New `Play` folder: 24 files, 2.2k lines, one XAML (`PlayTransportBar`). `PlaySessionService` takes `IProjectContextService`, is registered in the workspace child, and `WorkspaceViewModel` implements `IPlayWorkspace` | New cut C16 (section 3); Play becomes a Phase 4 session cluster |
| Folder inventory                               | `Diagnostics` (scene and runtime operation results) and `Utils` (thumbnails) are not placed by the spec                                                                                                                 | Placed in section 3                                             |
| Inspector 118 files, 15.5k lines               | 124 files, 16.2k lines after the responsive-row rebuild and script slots                                                                                                                                                | None; the Inspector stays one project                           |
| About 147 `ActiveProject` references, 40 files | 263 references (tests included) in about 43 production files                                                                                                                                                            | Phase 4 cluster sizes grow; migration stays compile-enforced    |
| Phase 0 sites                                  | All still present: `Program.cs` second `RegisterViewsAndViewModels` (124), swallowed `ContainerException` (126), duplicate locator and converter (312, 313), eager `AssetRedirects` (482-484)                           | Done in Phase 0                                                 |
| F13 Docking statics                            | `Dockable.Factory` static registry still present                                                                                                                                                                        | Phase 5 unchanged                                               |

Tooling facts the spec does not mention, which shape every new UI project:

- Views are paired with view models by `[ViewModel(typeof(...))]`, consumed by
  `Mvvm.Generators`. Every new UI project carries the generator and attribute
  references that `Oxygen.Editor.WorldEditor.csproj` has today.
- `Themes/Generic.xaml` is one dictionary for the whole monolith. Each editor
  project gets its own `Themes/Generic.xaml`, merged by the application through
  the editor's module (spec risk table, last rows).
- The application has no test project, so Phase 0 and every slice that changes
  the composition root (S1.2, S1.4a, S1.7, S1.10, S1.11 and S1.13) are verified
  by the reviewer's editor build.

## 2. Decisions made for implementation

These follow from the spec and the repository rules; they are recorded so the
slices do not reopen them.

1. **Docs move with the code.** `PROJECT-LAYOUT.md` (sections 5, 9 and 12) and
   `ARCHITECTURE.md` (sections 6 and 14) are updated in the same commit that
   creates each project, not in Phase 7. Phase 7 keeps only the scope-chain
   reference and the retirement of the spec into it. Section 12 of
   `PROJECT-LAYOUT.md` gains the per-editor rule that the spec's granularity
   decision establishes (a panel with its own module and tests is a project),
   replacing "a small panel inside the world editor is not enough reason".
2. **`Oxygen.Editor.Assets` keeps its name.** Its ownership row states the
   difference from `Oxygen.Managed.Assets`: the editor's live view of a
   project's assets (catalog, feed, identity rules, the rules of properties that
   reference assets) over the content pipeline, versus the asset formats, file
   types and cook primitives shared with the engine and its tools.
3. **Phase 1 module discovery.** Until Phase 2 introduces scopes, each feature
   exposes a module object. The application registers the module instances at
   the root; the workspace window, once it is in the Shell, runs every module's
   window-layer registration into its existing child container. This removes the
   workspace's knowledge of feature types (C10) without changing resolution
   behavior, and Phase 2 swaps the target container, not the modules.
4. **Contracts are written once.** As the spec requires, new contracts land in
   their target shape in Phase 1 step 2, behind adapters over today's
   implementations. A later phase that needs to change a contract stops and
   raises it.
5. **Test placement.** Each new project follows the canonical shape
   (`README.md`, `src`, `tests`), with `tests/Unit`, `tests/Integration` and
   `tests/Benchmarks` projects as needed, and its own fixtures in
   `tests/TestSupport`. A test lives with the module whose behavior it checks;
   other modules fake that module's public ports or use the real ones, and never
   share its fixtures. Internals are visible only to the module's own tests
   (architecture decision 4).
6. **Domain modules start with a design.** Each domain module slice begins with
   a design section for review, as [assets-module.md](../lld/assets-module.md)
   did for Assets: type-by-type ownership, public API, ports, dependencies and
   test plan. Code follows the reviewed design.
7. **Move, then rename.** A move commit keeps type names and namespaces so the
   compiler checks the move; a rename commit to the architecture's names follows
   it (architecture decision 6).
8. **#69 also delivers the behavior of the documents design.** The structural
   moves and the user-visible changes it specifies (the confirmations of its
   scenario 2.9, cook on change with its quiet window, cooking outside changes,
   the History panel) are slices of this plan. Visible UI stays uncommitted
   until the reviewer accepts it in the running editor.
9. **A slice moves code only into its final home.** Work whose owner does not
   exist yet waits for the slice that creates that owner, and existing code
   stays where it is until then. No slice adds a flag, a copy or an adapter that
   a later slice has to rewrite or remove; code a later slice only moves (a port
   adapter written in the World Editor before LivePreview exists) is allowed.
   Each slice is checked against this rule before it starts.

## 3. Placement of what the spec does not place

| Code                                                                                                                                          | Target                           | Cut                                                                                                                                                                               |
| --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IRuntimeProblemSource`, `PlayRequest`, `PlayStatus*`, `PlayActivity`, `PlayFailure`                                                          | `LivePreview`                    | Play is runtime work; Problems consumes `IRuntimeProblemSource` only                                                                                                              |
| `PlaySessionService`, `PlaySessionSettingsService`, `PlayerConnection`, `PlayerSettingsSync`, `RuntimeProblemReader` and their tests          | `LivePreview`                    | Registered by the LivePreview module, not by `WorkspaceViewModel.Play`                                                                                                            |
| `ICookingWorkspaceActions` (C12, **C16**)                                                                                                     | Shell                            | Replaced by Shell placement and commands (shell design section 4); selecting a node goes through Scenes. `WorkspaceViewModel` keeps implementing both until S1.12 moves `Cooking` |
| `PlayTransportBar`, `ScenePlayToolbarViewModel`, `PlayerWindowModeIndexConverter`, `PlayerWindowSizeOption`, `PlayPrimaryAccent`, `PlayPanel` | `SceneEditor`                    | Moves with the scene editor (`SceneEditorView` hosts the bar)                                                                                                                     |
| `Diagnostics/SceneOperationResults`                                                                                                           | `Scenes`                         | Scene operation-result vocabulary                                                                                                                                                 |
| `Diagnostics/RuntimeOperationResults`                                                                                                         | `LivePreview`                    | Runtime operation-result vocabulary                                                                                                                                               |
| `Utils/ThumbnailGenerator`, `ThumbnailTemplateSelector`                                                                                       | `SceneExplorer`                  | As the spec lists for the explorer's thumbnails                                                                                                                                   |
| Script checks of `Scripting/ScriptAnalysisService`, `ScriptToolingPaths`, `ProjectScriptTooling`                                              | `Scripting`                      | The script family's facts and tooling (asset family modules); the native analysis reaches them through `IScriptAnalyzer`, which LivePreview implements                            |
| Native call of `Scripting/ScriptAnalysisService`                                                                                              | `LivePreview`                    | The only domain module that references Runtime and Interop                                                                                                                        |
| `Scripting/InputFileCheck`                                                                                                                    | `Input`                          | The input family's checks (asset family modules)                                                                                                                                  |
| `Scripting/ScriptSlotEdits`                                                                                                                   | `Scenes`, rules in `Scripting`   | Edits a scene's script slots, validated by `ScriptSlotRules`                                                                                                                      |
| Editing part of `MaterialDocumentService` (properties, history entries, persistence)                                                          | `Materials` and `MaterialEditor` | Documents design decision 7: a document type is a family model and an editor                                                                                                      |
| Scene-specific parts of Projects (`SceneSourceVersion`, `GetSceneSourceVersion`, `SceneAssetRenameOperation`, scene staging)                  | `Scenes`                         | Documents design decision 7; Projects keeps asset-to-file resolution only                                                                                                         |
| `OpenDocumentCookInputs`, `CookDocumentRegistry`, cook read leases                                                                            | removed                          | Documents design 5.3.3: the cook reads saved files only and asks `IUnsavedDocuments` which inputs are unsaved                                                                     |
| `IAutomaticCookService.NotifySaved`, `NotifyRenamed`, the automatic-cooking pause                                                             | removed                          | Documents design 5.3.1 and 5.3.2: the cook hears of changes from `IAssetCatalog.Changes`; **Cook on change** is a project setting                                                 |

## 4. Execution slices

Each slice is one thematic commit (or a short series) that builds its owning
test projects and passes the tests that cover the moved code. The reviewer's
editor build closes each phase. Verification uses 64-bit MSBuild on the owning
test projects and `traverse Invoke-Tests --start <project>/tests` on the moved
tests only.

### Phase 0: startup hygiene

- **S0.1** Remove the duplicate `IViewLocator` and `ViewModelToView`
  registrations and the second `RegisterViewsAndViewModels` call; one owner for
  each (the `Bootstrapper`/`WithMvvm` registration stays). Replace the swallowed
  `ContainerException` in `Main` with a reported, aborting startup failure.
  Gate: reviewer's editor build starts to the Project Browser and opens a
  project.

### Phase 1: project decomposition

Cook on change comes first: it is behavior the documents design changes, and
every part of it has its final home today. Then the domain modules, because they
are non-UI and the features depend on them; the Shell comes before the features,
because a feature leaves the World Editor only when the Shell offers what it
needs instead of a `WindowId`. Every slice follows decision 9.

- **S1.1 Assets** (C8): done. `Oxygen.Editor.Assets` owns the asset catalog and
  feed, the identity rules and the rules of properties that reference assets,
  with its `IAssetRuntimeStatusSource` port; the file types table is in
  `Oxygen.Managed.Assets`; the asset messages are gone. Its registration module
  waits for Phase 3. Remaining ([assets-module.md](../lld/assets-module.md)):
  the badge properties of `AssetEntry` (`PrimaryBadge`, `PrimaryBadgeTooltip`,
  `PackagedBadge`, `GetBadge`) become `AssetStatusText` functions the explorer
  presents; the feed's callers outside the module (material editor, geometry,
  exposure, scene references, Problems) stop calling `RefreshAsync`, since the
  feed follows its own inputs; the feed takes unsaved state from
  `IUnsavedDocuments` instead of `ICookDocumentRegistry` with S1.11.
- **S1.2 Cook on change** (documents design 5.3.1 and 5.3.2, and the quiet
  window of 5.3.6). The automatic cook listens to `IAssetCatalog.Changes`
  instead of being told about saves. Changes to cookable files under authoring
  roots open the quiet window. When it closes, a run waiting in `NeedsSave` for
  a changed file resumes; for the others, the cook waits until no cook of the
  project is in progress and cooks each changed asset whose published output
  does not match what is on disk, so a cook that already covered the change
  (Play's) is not repeated. Each asset gets today's automatic cook, with at most
  one in flight. Opening a project cooks nothing: the catalog reports the files
  it starts listing as `Listed`, not as changes. The **Cook on change** project
  setting, stored in the project manifest and changed without reactivating the
  project, replaces the in-memory pause; the Content Explorer's toggle edits it.
  `NotifySaved` and `NotifyRenamed` go, with their calls in the scene command
  service, scene rename, the material document service and script analysis. So
  that no change goes unseen, `FileSystemAssetCatalog` reports a file replaced
  by an atomic save as updated and every known file as updated after a watcher
  overflow, and `AssetCatalog` keeps watching the authored folders across
  publications and reports only the differences when it installs new catalogs.
  Every cook logs when it is queued, starts, waits for a save and finishes. The
  cook is registered beside the catalog, in the workspace's scope. Until S1.9,
  scripts and input files cook like any file, and one with problems fails its
  cook. Gate: Managed.Assets, Projects, Assets and ContentPipeline tests, the
  tests of the callers, the reviewer's editor build and acceptance of the
  toggle.
- **S1.3 Foreground cooks** (rules 4 to 6 of documents design 5.3.6). When any
  foreground work waits for the writer, a running cook on change is cancelled
  and stops at its next cancellation point, which never publishes a partial
  result; one already publishing finishes. It ends as stopped, and cook on
  change checks its asset again after the foreground work, cooking it only if it
  is still stale. A run cannot simply run again: its operation folder holds the
  stopped attempt's snapshots and journal. A request joins a pending cook of the
  same scope or of a folder or project that covers it, never a reimport and
  never a run that has started. A foreground request that joins a pending cook
  on change of the same scope gives it its own work, so an explicit cook or
  reimport is no longer reduced to the automatic one. Every stop and join is
  logged. Gate: ContentPipeline tests.
- **S1.4a Global history** (documents design decision 1). `Oxygen.Editor.Shell`
  is created with its first part, `Documents/`: `IHistoryService` holds the
  session's global history. It is registered in the workspace's scope, beside
  cook on change, so a project's history ends with it. ContentPipeline records
  each relocation into the history its caller gives it, as one change whose undo
  runs the reverse relocation and records the reverse of that for redo. The
  Content Browser's own single undo goes; its result bar offers Undo and Redo
  over the global history, which Ctrl+Z reaches with command routing in S1.10. A
  copy, delete or scene command no longer clears it: an undo that a later change
  made invalid is rejected without changing anything and stays listed. A
  relocation, copy, delete or scene command no longer cooks the whole project:
  the changed files reach cook on change through the catalog, and an open scene
  shows its relocated nodes again in the runtime once the asset feed reports
  their new assets mounted, whoever cooked them. Cook on change waits until the
  project writer is idle (`ICookRunService.WhenWriterIdleAsync`), so it never
  reads statuses while a relocation is half done, and it skips a source whose
  file is gone. A status reads source files from disk, sharing them with any
  writer, and lists unsaved documents from the registry's published state; it no
  longer waits for save gates or compares an open document's saved hash. Gate:
  Shell, ContentPipeline, Assets, Content Browser, Material Editor and World
  Editor tests, the reviewer's editor build and acceptance of Undo and Redo.
- **S1.4 Pipeline workflows out of UI** (architecture section 8). The relocation
  service keeps its reference index current itself: any asset catalog change
  makes the next search read the files again, so no caller invalidates it. It
  also reviews a delete (`IAssetRelocationService.FindReferrersOutsideAsync`):
  the files outside the deleted items that use them, read again first.
  ContentPipeline's `Authoring/IAssetAuthoring` proposes the folder and the
  first free name of a new script or input file, checks a chosen name against
  the asset catalog, and writes the template through `IAtomicFileStore` only
  where no file exists, so a file the catalog has not seen yet is never
  overwritten; `AssetTemplates` moves with it. Both services report the files
  the editor changed as `FilesChanged`, the relocation service now including the
  files and folders a copy creates, and script authoring each file it creates
  before `CreateAsync` returns. The asset feed follows both reports and reads
  the catalog again at once, whoever asked for the change, so an undo from
  anywhere and a new script's reveal see the change before the file watcher
  reports it. The Content Browser keeps the prompts, the result messages, the
  reveal and the code editor launch, and no longer refreshes its rows after a
  change, except after a scene command. The sources tree and the asset list
  share one clipboard, so a folder copied or cut in the tree pastes as a folder;
  the tree cuts, copies, pastes and deletes folders from its context menu, and
  pastes with Ctrl+V and deletes with Delete, through the same workflow as the
  asset list. Ctrl+X and Ctrl+C cut and copy the asset list's selection
  wherever the user works in the browser. The asset catalog knows the
  folders of its roots: a folder created, deleted or renamed outside the editor
  makes it read the root again, so the files inside follow, and reports the
  folder through `IAssetFolderSource`, which the tree reloads on. Each file
  belongs to one catalog: the project folder's catalog leaves out the mounts
  inside the project, so no change is reported twice. A selected folder that no
  longer exists, deleted or reached again through Back or Forward, shows its
  nearest existing folder instead. Cook on change treats a moved or deleted
  source as a change: an automatic cook of a source that is gone, and that has
  published output, publishes the content again without it. A mount whose every
  product was retired, and that nothing in the cook goes into, leaves the
  publication as if it was never cooked: the preview unmounts its root and its
  generation is reclaimed. The engine's canonical virtual path accepts the space
  and parentheses as well as letters, digits, `-`, `_` and `.`, and the editor
  accepts exactly those characters in a name, so every name it creates, such as
  `New folder (2)`, cooks. Gate: ContentPipeline, Assets, Content Browser and
  World Editor tests.
- **S1.4c Removing one cooked product.** A deleted or renamed source, or an
  import whose outputs moved to another group, no longer rebuilds its whole
  mount. The engine's `LooseCookedWriter` removes asset entries and auxiliary
  files from a loose cooked index (`RemoveAssetDescriptor`,
  `RemoveAuxiliaryFile`): it publishes the index without them, then deletes
  their files; resource table entries stay, as they do when a cook replaces an
  asset. The ImportTool's `remove-cooked` command runs it from a JSON request.
  The cook repair lists each inherited root's retired outputs, and the cook
  removes them from the staged copy of the root, under its fresh generation
  identity, before it cooks anything into it; the other products of the mount
  are reused. A product that used a removed one is cooked again, so a scene that
  lost its material reports it instead of keeping a reference to cooked content
  that is gone. A root whose every product leaves, and that nothing in the cook
  goes into, still leaves the publication. Gate: engine writer tests,
  ContentPipeline native cook repair tests, and the reviewer's check that
  deleting one asset of a large mount no longer cooks the rest.
- **S1.5 Document kinds** ([document-kinds.md](../lld/document-kinds.md)).
  `Oxygen.Editor.Documents` holds what every document has and does, without
  windows or tabs: its file, the version it read, dirty from its history's
  position, missing and changed on disk, its history and its content; opening
  once per file with the kind registered for the file's suffix, saving against
  the version it read, saving a copy, hearing that its file changed (quiet
  reload, changed on disk, missing), reloading, Keep Mine and following a
  relocation. A kind only decodes its file, captures and encodes what to save,
  finishes its edits in progress and applies a relocation to its model; it holds
  no version or history. TimeMachine's history exposes its position, which undo
  and redo restore. Projects maps asset URIs to files and back, in the asset
  catalog's URI form, and every copy of that mapping outside the scene parts of
  Projects uses it; those parts wait for S1.6. `Oxygen.Editor.Runtime` loses its
  unused reference to Documents. Gate: TimeMachine, Documents and Projects tests
  and the tests of the modules whose copies are replaced.
- **S1.6 Scenes** (C1, C2, C3 model side, C4 contract, C7, C15;
  [scenes-module.md](../lld/scenes-module.md)). `Oxygen.Editor.Scenes`, a class
  library with no WinUI, takes the command service with its edit sessions,
  results, edit records and descriptors, selection, the mutator and the
  organizer without its unused adapter code, node lock and visibility behind an
  interface, and the `ISceneEngineSync` port with value types of its own; it
  implements the scene document kind (S1.5). The command service loses its
  `WindowId` and its messages, which become .NET events. Scenes become
  documents: the World Editor opens them through `OpenDocuments`, and Projects'
  scene members, versions and snapshots go. A scene's name is its file name,
  scenes live in any folder of an authoring mount, and the relocation service
  renames, moves, copies and deletes them like any asset, with file reports and
  global undo for all but delete; `SceneAssetCommandMessage`,
  `SceneAssetRenameOperation` and the project's scene list go. New Scene and the
  scene rules of the relocation service live in ContentPipeline. Done: the
  command service, the gate, the mutator and the organizer are replaced by scene
  edits ([scene-edits.md](../lld/scene-edits.md)): edit records run by each
  document's `SceneAuthoring`, with atomic batches instead of change sets, and
  an engine projector that follows its change stream. The document identity
  design ([document-identity.md](../lld/document-identity.md)) came in between.
  Gate: Scenes tests without the WinUI host, the tests of the callers, the
  reviewer's editor build and acceptance in the running editor.
- **S1.7 LivePreview** (C4 implementation, C11, C13, Play): done.
  `Oxygen.Editor.LivePreview` takes engine sync, content demand (without its
  `WindowId`), mount changes, the runtime session start taken out of
  `WorkspaceViewModel`, the Play services and the runtime problem source, and
  the adapter for the port of Assets; it previews open scene documents by
  observing them (`IScenePreview`), so Scenes knows no engine. It is the only
  domain module that references `Oxygen.Editor.Runtime` and `Oxygen.Interop`.
  Design: [live-preview.md](../lld/live-preview.md). Gate: LivePreview tests and
  the reviewer's editor build.
- **S1.8 Asset family modules.** Design:
  [asset-family-modules.md](../lld/asset-family-modules.md). Each family's
  model, rules, problems, facts, templates and codes move into its domain
  module; `WorldEditor/Scripting` is deleted.

  Already in place, not part of this slice: the formats in Managed.Assets
  (`MaterialSource`, `TextureSource`, `InputSource` with `InputSourceCodes`
  under `OXE.INPUT.`), `AssetProblem` and `AssetFormatException`,
  `Oxygen.Editor.Materials` with `Material`, its document kind, edits and codes,
  and every identity, reference and collision rule of
  [asset-identity.md](../lld/asset-identity.md).

  Five commits, each building its owning projects and passing their tests:
  1. **Problem source.** `IAssetProblemSource` (`Problems`, `ProblemsChanged`)
     in Managed.Assets `Diagnostics/`.
  2. **Textures.** New `Oxygen.Editor.Textures`: `Texture` over `TextureSource`,
     and `TextureImport` from ContentPipeline
     `Import/TextureSourceAssetImporter.cs` (cube face detection,
     `HasSourceImage`, `ReadCubeDescriptor`). Callers switch: ContentBrowser
     `Importing/TextureImportDialogViewModel.cs` and the assets pane,
     WorldEditor `Inspector/Environment/CubemapPickerModel.cs`, MaterialEditor
     `MaterialEditorViewModel*.cs` and `MaterialTextureChannel.cs`.
     `TextureSourceAssetImporterTests` move to Textures Unit.
  3. **Input.** New `Oxygen.Editor.Input`: `InputContainer`, `InputAction`,
     `InputMappingContext`, `InputProblem`, the input rules,
     `StartingContextRules`, `InputFacts` as `IInputSources`, and
     `InputTemplates`. It takes ContentPipeline `InputSourceFiles.cs`,
     `InputContextSources.cs` and `InputContextSource.cs` (resolving files
     through Projects' `ProjectContext.Find` instead of the pipeline's
     `RelocationPaths`), WorldEditor `Scripting/InputFileCheck.cs` and the input
     half of `Scripting/ScriptAnalysisService.cs`, and ContentBrowser
     `Scripts/InputDeclaration.cs`. Callers switch: ContentPipeline
     `SceneDescriptorGenerator.Starting.cs` and the relocation planner's input
     renames, Scenes `Edits/Settings/SetStartingInputContexts.cs`, WorldEditor
     `Inspector/SceneNodeEditorViewModel.cs`, the ContentBrowser assets pane.
     `InputFileCheckTests` move to Input Unit.
  4. **Scripting.** `Oxygen.Editor.Scripting` grows:
     - Model: `Script`, `ScriptParameter`, `ScriptParameterKind` from
       ContentBrowser `Scripts/`; `ScriptProblem : AssetProblem` loses
       `IsRuntime`, and `ScriptProblemSeverity` goes; `ScriptDiagnosticCodes`
       moves from Managed.Core.
     - Rules: `ScriptSlotRules` and `ScriptFieldAddress`, from the checks of
       WorldEditor `Scripting/ScriptSlotEdits.cs` and ContentPipeline
       `ScriptSlotDiagnostics.cs`.
     - Facts and ports: `ScriptFacts` as `IScripts`, from the script half of
       WorldEditor `Scripting/ScriptAnalysisService.cs` (+ `.Logs.cs`);
       `IScriptAnalyzer`, implemented by a new LivePreview
       `Scripting/NativeScriptAnalyzer.cs`; `IScriptLibrary`, implemented by
       ContentPipeline in place of `Inspection/IScriptDeclarationProvider.cs`,
       `CookedScriptDeclarations.cs`, `CookedScriptReport.cs` and
       `ContentPipelineService.ScriptDeclarations.cs`.
     - Tooling: `ProjectScriptTooling.cs`, `ScriptToolingPaths.cs`
       (WorldEditor), `ScriptOperationResults.cs` (ContentBrowser).
     - LivePreview: `RuntimeScriptProblem : ScriptProblem`;
       `Play/IRuntimeProblemSource.cs` extends `IAssetProblemSource`;
       `PlaySession*.cs` and `RuntimeProblemReader.cs` produce it.
     - Scenes: `Edits/Scripting/*` (`AddScriptSlot`, `RemoveScriptSlot`,
       `MoveScriptSlot`, `SetSlotScript`, `SetSlotExecutionOrder`,
       `SetScriptValue`, `ResetScriptValue`, `RemoveStaleScriptValues`),
       validated by `ScriptSlotRules`; `SetScriptSlots` stays.
     - Cook: ContentPipeline `ScriptSlotDiagnostics.cs`,
       `SceneDescriptorGenerationResult.cs`,
       `SceneDescriptorGenerator.Scripting.cs` and
       `ContentPipelineService.Scripting.cs` report `ScriptSlotRules.Check` and
       keep only the mapping to `DiagnosticRecord`.
     - Consumers (LLD section 9): the WorldEditor inspector's script cards,
       `ScriptDeclarations.cs` (deleted), the Problems panel over every
       `IAssetProblemSource`, `WorkspaceViewModel.Scripting.cs` and
       `.Cooking.cs`; the ContentBrowser assets pane and details.
     - Deleted: `WorldEditor/src/Scripting`; in ContentBrowser `Scripts/`,
       `ScriptAnalysisSnapshot.cs` and `IScriptAnalysisService.cs` (the code
       editor files stay for S1.10).
     - Tests: Scripting Unit (new, takes `ProjectScriptToolingTests`); Scenes
       Unit takes `ScriptSlotEditsTests`; WorldEditor keeps
       `ScriptingViewModelTests` and `ProblemsViewModelTests` over fakes of the
       ports; LivePreview Integration.UI tests `NativeScriptAnalyzer`.
  5. **Templates.** `AuthoredAssetTemplate` (Projects) replaces ContentPipeline
     `Authoring/AuthoredAssetKind.cs` and `AssetTemplates.cs`;
     `ScriptTemplates`, `InputTemplates`, `MaterialTemplates` and
     `SceneTemplates` own the texts and each family's template;
     `Authoring/AssetAuthoring.cs` and ContentBrowser
     `Authoring/AuthoredAssetCreation.cs` take a template. `AssetAuthoringTests`
     run over the family templates; the scene and material content checks move
     to Scenes and Materials tests.

  Gate: the tests of LLD section 14 and the reviewer's editor build and check
  (script cards, Play section, Problems with analysis and runtime problems, the
  Content Explorer's script and input details, texture import, the cubemap
  picker).

- **S1.9 Cook on change over checked inputs** (documents design 5.3.2, and rules
  1 to 3 of 5.3.6). In place from S1.2: the **Cook on change** project setting,
  the cook listening to `IAssetCatalog.Changes`, and the 500 ms quiet window up
  to 5 s. What remains:
  - Cook on change skips an input with problems, read from the family facts of
    S1.8; the change that fixes them cooks it.
  - One automatic run per quiet window: `AutomaticCookService` queues one cook
    per changed source today; it queues one run covering them, which new changes
    join while it waits for the writer.
  - Changes that arrive while that run cooks collect into the next run, instead
    of each source cooking once more on its own.

  A cook run fails as a whole when one input fails, which is why the single run
  waits for the skip. Landed: errors in the script and input facts skip the
  input, the run names it, and the facts reporting no errors cook it; a saved
  script cooks on change; a run of several inputs is a `Changes` run, which
  Retry cooks again as one requested run; a source moved or deleted before the
  run starts leaves it, and one that cannot be a cook input is not cooked.
  Gate: ContentPipeline and LivePreview tests.

- **S1.10 Shell** (C9, C10, C12, C16). In place: the global history
  (`IHistoryService`, S1.4a), and `DocumentArea`, `DocumentTab` and
  `DocumentEditorDescriptor` placing documents in tabs. `Oxygen.Editor.Shell`
  grows to the layout of shell design section 7 and receives the generic
  workspace machinery of `WorldEditor/Workspace`. Its document service
  (`IDocumentService`, `DocumentService`) replaces `EditorDocumentService`,
  `DocumentHostViewModel` and `DocumentManager`: it opens a file once, places
  the document, routes Undo and Redo to the per-document histories (created with
  each open document by `OpenDocuments`) and the global one, the confirmations
  of documents design 5.2.9, the missing and conflict banner, and following
  relocated files. The close coordinator, the close and conflict prompts,
  `DocumentCloseItem` and the input committer move into the Shell with it: they
  take the `DocumentTab` instead of `IDocumentMetadata` and lose their
  `WindowId`, and the close prompt's check that a tab is a `DocumentTab` goes.
  The refusal of every detach moves from `EditorDocumentService` into the
  Shell's document service, which allows detach once document windows can
  receive a tab (document-identity.md 5.7). Aura's contract in
  `DroidNet.Documents` is renamed `IDocumentTabSource`. The other contracts land
  in their target shape (decision 4): contributions (panel and editor
  descriptors, `IDocumentEditorProvider`), part services (placement, commands,
  close and exit) and desktop integration (`CodeEditorService`, `IAssetShell`).
  Their first implementations adapt today's Aura, Docking and Routing; the
  library changes of shell design section 9 come in Phase 5. Notifications,
  menus, the status bar and the command palette are not part of #69 (shell
  design section 8). Gate: Shell tests and the reviewer's editor build.
- **S1.11 Unsaved documents** (documents design 5.3.3).
  `Oxygen.Editor.Documents` declares `IUnsavedDocuments`, the Shell's document
  service implements it and ContentPipeline consumes it. Play, Cook and
  relocation ask before they run. The `NeedsSave` state, `CookDocumentRegistry`,
  the read leases (`Document.AcquireReadLease`) and `OpenDocumentCookInputs` are
  removed, so every cook reads what is on disk and a cook on change no longer
  waits for a dirty document (scenario 5.2.6). Relocation asks instead of
  refusing (`AssetRelocationService.RejectUnsavedDocuments`). Gate:
  ContentPipeline tests, the reviewer's editor build, and the reviewer's
  acceptance of the confirmations in the running editor.
- **S1.12 Features**, a move commit and a rename commit each, with the feature's
  module, theme dictionary and tests, in this order: `Output` (shows
  `PlaySession.LogReceived`, which the workspace bridges to `OutputLogBuffer`
  today), `Problems` (selects a runtime problem's node through
  `ISceneSelectionService` and reveals itself on a runtime error, which the
  workspace does today), `Cooking` (its `ICookingWorkspaceActions` goes: the
  panel saves and opens documents through the Shell's document service, shows
  panels through placement and selects a node through Scenes, and the workspace
  stops implementing it), `CookedInspector` (from `Inspection`),
  `SceneExplorer` (C3 explorer side), `SceneEditor` (C14, Play UI, and its
  preview, viewport and transform tool settings), `SceneInspector` (from
  `Inspector`); then the Material Editor registers its editor provider, the
  Content Browser becomes `ContentExplorer` (its relocation and creation
  workflows go to ContentPipeline, `IAssetShell` to the Shell's desktop
  integration) (its left pane, `ProjectExplorer` today, becomes
  `ContentFolders`) and the Project Browser becomes `ProjectExplorer`. The new
  `TextureEditor` replaces the Content Browser's texture import dialog, and the
  new `History` panel (documents design decision 1) comes last, as a passive
  panel over the Shell's histories. After each, the workspace stops registering
  that feature's types and the document host stops constructing it. Gate: that
  feature's tests; UI test failures go to the reviewer; visible changes wait for
  the reviewer's acceptance (decision 8).
- **S1.13 Dissolve the World Editor.** Nothing is left in
  `Oxygen.Editor.WorldEditor`; the project and its test projects are deleted,
  `Oxygen.Editor.World` drops its internals grant to it, and the application
  composes the Shell and the modules (decision 3). Gate: the reviewer's editor
  build.
- **S1.14 Test boundaries.** Most of the internals grants and linked fixtures go
  with the moves of the earlier slices, because a test moves with the code it
  checks. What is left is removed here: no module grants its internals to
  another module's tests, each remaining test is rewritten over public API,
  faked ports or the real services, or moves into the tests of the module whose
  behavior it checks, and no test project links another module's fixtures. Gate:
  the touched test projects.
- **S1.15 Enforcement.** `tooling/tests/test_editor_module_boundaries.py`
  checks, from the project and `AssemblyInfo` files, the layer rules of
  architecture section 5.2, the engine rule of its section 5.4 and the internals
  rule of its decision 4. The violations the tree still has are listed in the
  test with the slice that removes them (S1.13, S1.14); an entry that no longer
  occurs fails the test. Hand-built test containers move to the shared
  host-equivalent builder in `TestHelpers` with S3.1, whose registration modules
  it composes. Gate: tooling tests.

  Test practice, decided with the reviewer during S1.8 (Textures is the pilot)
  and applied by every slice that touches a test:
  - **Module test kits.** A module that other modules test against publishes
    `projects/<Module>/testing/<Module>.Testing.csproj`, referenced only by test
    projects, as `TestHelpers` is: builders of its public types and in-memory
    fakes of its ports, maintained by the module and checked against the real
    service. Other tests reference the kit; they do not hand-build the type,
    re-mock the port in each test, or link the module's `testsupport` files.
    `Oxygen.Editor.Projects.Testing` (`TestProject`: a project and its mount
    folders on any `IFileSystem`, `WriteFile` and `PathOf` by virtual path) is
    the first; Scripting (`IScripts`, `IScriptAnalyzer`,
    `IScriptLibrary`) and Input (`IInputSources`) follow with S1.8.
  - **Moq.** A test may mock a port with Moq when its owner publishes no kit
    fake for it and the test needs only a few canned answers, a thrown failure
    or a check that a call was made. A port whose kit has a fake is used through
    the fake. A port that tests keep mocking with the same setups, or that needs
    state across calls (a store, events, holds), gets a fake in its owner's kit
    instead. Moq mocks ports only, never models or the module under test.
  - **File system.** Unit tests of logic over files run on Testably's
    `MockFileSystem`; the real disk is for integration tests (atomic replace,
    watcher timing, locking, native tools). Code a slice writes or moves takes
    `IFileSystem`.
  - **Models** take required identity members and optional `init` members, not
    long positional constructors.
  - **Codes, not text.** Every refusal in code a slice moves carries a contract
    code (`TextureImportCodes`, `TextureImportException.Code`), and tests assert
    the code.

  What the S1.8 survey measured (651 test files), for this slice to finish:
  - Project construction: every test, `testsupport` file and test probe makes
    its project with `TestProject` (`Create` over any file system, `Open` for a
    project loaded through the project manager, `SaveProjectFile` and `InfoOf`
    for its saved form), directly or through the per-module builders
    `AssetWorkspace`, `RelocationWorkspace`, `BrowserTestData`,
    `GenerationScenario`, `CookCoordinatorScenario`, `SceneTabs`,
    `CookWorkspace` and `PublicationProject`. Only the Projects module's own
    tests build `ProjectContext` by hand, so Phase 4's removal of
    `IProjectContextService` changes the kit only.
  - Tests build the content pipeline and the asset feed through builders that
    default what a test does not vary: ContentPipeline's `CookScenario.CreateService`
    and `NativeCook`, and Assets' `AssetFeeds`. A new constructor dependency
    changes the builder only. The shared host-equivalent builder replaces them
    once registration modules exist (S3.1). What still builds them by hand: the
    tests whose subject is `SceneDescriptorGenerator` or
    `CookDependencyDiscovery`, a few tests that share one native API between
    services, and `NativeContentPipelineFixture`, which other modules link.
  - A project's files are read through `ProjectContext.FileSystem`; code that
    holds a project takes no second file system for them. Editor source still
    uses `File`/`Directory` statics in 44 files. The 30 in ContentPipeline are
    the cook's native side (input snapshots, staging, publication, cooked roots
    and the cross-process leases), which the native tools and other processes
    share on the real disk. The rest read no project files through them (SDK
    schemas, the player, paths the user picks outside the project, the shell)
    except the Content Browser's folder tree, which is built on Storage. About
    30 unit test files make their own `%TEMP%` root, mostly for those native
    paths. Unifying Storage and `IFileSystem` is a
    separate decision, not part of #69.
  - About 45 test files assert message, tooltip or title text.
  - View models with long hand-wired constructors spread every new dependency
    over their tests: `AssetsViewModel` (13 arguments, 4 test sites),
    `MaterialEditorViewModel` (12, 3 sites), `SceneNodeEditorViewModel` (15, 2
    sites); `EnvironmentViewModel` takes only optional arguments. Three of those
    sites are fixtures linked as source into other test projects, each of which
    then needs the fixture's packages. The shared host-equivalent builder
    constructs them from registrations instead.
  - Test projects that link another module's `testsupport` files: WorldEditor
    Unit.UI (ContentBrowser `BrowserControls`, `BrowserQuerySupport`,
    `BrowserTestData`; ContentPipeline `EngineContentPakFixture`; Managed.Assets
    `LooseCookedIndexFixture`), Benchmarks.UI and Integration.UI
    (ContentPipeline `NativeContentPipelineFixture`, Managed.\* fixtures),
    ContentBrowser UI, LivePreview Unit and Integration.UI, Assets Unit,
    Integration and Benchmarks (`EngineContentPakFixture`), and MaterialEditor
    (`NativeContentPipelineFixture`, `TemporaryNativeArtifacts`). S1.14 removes
    them.

### Phase 2: scope factory and topology test

- **S2.1** `DroidNet.Composition`: `ScopeFactory`, `ScopeHandle`, module and
  activation-list contracts, `RouterScopeModule`, with the spec 6.3 rules and
  their own tests (chain root > session > window > panel, optional dependencies,
  open generics, disposal).
- **S2.2** Topology test over the shared builder, with the reviewed
  `RegisterDelegate` allowlist.
- **S2.3** Workspace and Content Explorer children move onto the factory; the
  three project-bound preference services and the keyed settings manager move to
  the window module; `DockPanelViewModel`/`DockPanel` move to the window module;
  eager-resolve workarounds deleted; `WorkspaceViewModel` resolves only through
  its handle. Gate: topology and project-switch tests, reviewer's editor build.

### Phase 3: registration modules

- **S3.1** Split `Program.cs` into the Editor modules of spec 6.7, one subsystem
  per commit, including the Assets registration; eager resolves become
  activation lists. Hand-built test containers move to a shared
  host-equivalent builder in `TestHelpers` that composes these modules (the
  rest of S1.15). Gate: topology test, reviewer's editor build.

### Phase 4: Project Session

- **S4.1** `IProjectSessionHost`, `IProjectSession`, `ISessionEvents`,
  `CookPublicationRecovery` split, the Shell's histories bound to the session,
  session clipboard, and the reworked `ProjectActivationCoordinator` with its
  cancel, replace and navigation-failure contract.
- **S4.2** Clusters, one commit each: preferences, assets, content pipeline and
  cooking, scene stack, script tooling, Play. Each injects `ProjectContext`,
  deletes its `ProjectChanged` reset and uses the session lifetime token.
- **S4.3** Remove `CurrentProject` and delete `IProjectContextService`.
- **S4.4** Close and exit (shell design section 6): the Shell's close and exit
  becomes the only subscriber to Aura's `WindowClosing`. Window participants
  (the window's documents, today's `WindowCloseCoordinator`, and the saving of
  window placement, today's `WindowPlacementService` subscription) and
  application finalizers (engine shutdown, today's `EngineShutdownService`
  subscription, which loses its window counting) run in its prepare, commit and
  finalize phases; closing the last workspace window of a session returns to
  the Start window.
- Gate: the spec's Phase 4 tests, including the **open check** of the issue:
  close the last workspace window with the Project Browser open and assert the
  process stays up. If WinAppSDK ends the process, stop and raise the shutdown
  policy before continuing.

### Phase 5: Docking, windows, placement, panels

- **S5.1** Docking per `IDocker` (registries, id counter, messenger), no
  `Dockable.FromId` in editor code, dynamic workspace window targets. Docking
  library tests first.
- **S5.2** `IRetainedViewModel` in Routing, Aura and Router, with tests.
- **S5.3** In the Shell: window scope factory, window registry, placement over
  the new window kinds, layout store; the document service receives resolved
  requests.
- **S5.4** Panel registry and `IPanelHost`, then the six panels one commit each,
  Content Explorer first.
- **S5.5** Floating-window host proving the move contract.
- Gate: the spec's Phase 5 tests; the reviewer validates a second window and a
  moved panel in the editor. Visible UI stays uncommitted until the reviewer
  accepts it.

### Phase 6: service location and static messengers

- **S6.1** Remove `IContainer` from `SceneEditorViewModel`, `WorkspaceViewModel`
  and `ContentExplorerViewModel`, and the `ReturnDefault` paths. **S6.2**
  Replace `WeakReferenceMessenger.Default` and
  `StrongReferenceMessenger.Default`. **S6.3** Apply the disposal policy to
  every registration. Gate: topology test.

### Phase 7: documentation

- Replace the spec with the final topology reference and update
  `lld/project-workspace-shell.md` and `ARCHITECTURE.md` section 8.1. Section 2
  decision 1 has already kept the layout documents current.

## 5. Execution risks beyond the spec

| Risk                                                                                    | Handling                                                                                                                                                    |
| --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Concurrent editor work on the `editor` branch touches files being moved                 | Move one folder per commit and commit promptly; a move never carries unrelated edits                                                                        |
| `x:Bind` and `ms-appx` paths break silently after a XAML move                           | Rewrite resource URIs in the moving commit; each editor's UI tests load its views (spec risk table)                                                         |
| A moved type turns public and collides with an existing name in its new namespace scope | Namespaces are preserved (R6), so collisions surface at compile time in the moving commit                                                                   |
| An editor build of the application is the only check of composition-root changes        | Phase 0, S1.2, S1.4a, S1.7, S1.10, S1.11, S1.13, S2.3, S3.1 and Phase 4 end with the reviewer's build; no further slice starts on a broken composition root |
| Shell contracts are written before the library changes they will run on                 | The Shell's first implementations adapt today's Aura, Docking and Routing behind the target contracts; Phase 5 replaces the adapters, not the contracts     |

## 6. Status ledger

Status values: `done`, `in progress`, `not started`. Update the row in the commit
that lands the work. Checked against the tree on 2026-10-20.

| Slice                                       | Status        | Notes                                                                                           |
| ------------------------------------------- | ------------- | ----------------------------------------------------------------------------------------------- |
| S0.1 Startup hygiene                        | `done`        |                                                                                                 |
| S1.1 Assets                                 | `done`        | Remaining items listed in the slice move with S1.8 and S1.11                                    |
| S1.2 Cook on change                         | `done`        |                                                                                                 |
| S1.3 Foreground cooks                       | `done`        |                                                                                                 |
| S1.4a Global history                        | `done`        |                                                                                                 |
| S1.4 Pipeline workflows out of UI           | `done`        |                                                                                                 |
| S1.4c Removing one cooked product           | `done`        | `1e32de33f`                                                                                     |
| S1.5 Document kinds                         | `done`        |                                                                                                 |
| S1.6 Scenes                                 | `done`        |                                                                                                 |
| S1.7 LivePreview                            | `done`        |                                                                                                 |
| S1.8 Asset family modules                   | `done`        | See the sub-slice table below                                                                   |
| S1.9 Cook on change over checked inputs     | `done`        |                                                                                                 |
| S1.10 Shell                                 | `done`        | Consistent clipboard and delete commands across parts: #86                                      |
| S1.11 Unsaved documents                     | `done`        | Shell `UnsavedDocuments` implements the port; Play, Cook and relocation ask before they run     |
| S1.12 Features                              | `in progress` | See the feature table below                                                                     |
| S1.13 Dissolve the World Editor             | `not started` |                                                                                                 |
| S1.14 Test boundaries                       | `not started` |                                                                                                 |
| S1.15 Enforcement                           | `in progress` | Boundary test and project construction done; the shared host-equivalent builder lands with S3.1 |
| S2.1 to S2.3 Scope factory and topology     | `not started` |                                                                                                 |
| S3.1 Registration modules                   | `not started` |                                                                                                 |
| S4.1 to S4.4 Project Session                | `not started` |                                                                                                 |
| S5.1 to S5.5 Docking, windows, panels       | `not started` |                                                                                                 |
| S6.1 to S6.3 Service location and messenger | `not started` |                                                                                                 |
| S7 Documentation                            | `not started` |                                                                                                 |

### S1.8 sub-slices

| #   | Commit         | Status | Notes                                                             |
| --- | -------------- | ------ | ----------------------------------------------------------------- |
| 1   | Problem source | `done` | Landed with Input, its first implementer                          |
| 2   | Textures       | `done` | Pilot of the S1.15 test practice                                  |
| 3   | Input          | `done` | Landed before the Problems panel worked over every problem source |
| 4   | Scripting      | `done` | Scene slot edits name a slot by `ScriptSlotId`                    |
| 5   | Templates      | `done` | `AuthoredAssetTemplate` lives in Projects, below every family     |

Landed before the sub-slices, as part of the same direction: Materials module,
checked URIs, source-collision reporting from the cook (`5d5c2bc0e`).

### S1.12 features

| #   | Feature | Status | Notes                                                                                                                              |
| --- | ------- | ------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Output  | `done` | Features join the workspace through the Shell's `IFeatureModule`; Output reads the player's log through LivePreview's `IPlayerLog` |
