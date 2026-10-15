# Issue #69: DI scope topology and WorldEditor decomposition

Status: `planned` — execution plan, no code changed yet.

Issue: [#69](https://github.com/abdes/DroidNet/issues/69)

Design owner:
[di-container-topology.md](../review/di-container-topology.md) (the "spec"
below). This plan sequences the spec's phases into commit-sized slices, records
what changed in the code since the spec was written, and states the decisions
the spec left to implementation. It does not restate the target design.

## 1. Code drift since the spec

The spec was measured at `da292a237`; 36 commits have landed since. Verified
against the current tree:

| Spec claim                                     | Current tree                                                                                                                                                                                                            | Effect on the plan                                              |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| Folder inventory (F12)                         | New `Play` folder: 24 files, 2.2k lines, one XAML (`PlayTransportBar`). `PlaySessionService` takes `IProjectContextService`, is registered in the workspace child, and `WorkspaceViewModel` implements `IPlayWorkspace` | New cut C16 (section 3); Play becomes a Phase 4 session cluster |
| Folder inventory                               | `Diagnostics` (scene and runtime operation results) and `Utils` (thumbnails) are not placed by the spec                                                                                                                 | Placed in section 3                                             |
| Inspector 118 files, 15.5k lines               | 124 files, 16.2k lines after the responsive-row rebuild and script slots                                                                                                                                                | None; the Inspector stays one project                           |
| About 147 `ActiveProject` references, 40 files | 263 references (tests included) in about 43 production files                                                                                                                                                            | Phase 4 cluster sizes grow; migration stays compile-enforced    |
| Phase 0 sites                                  | All still present: `Program.cs` second `RegisterViewsAndViewModels` (124), swallowed `ContainerException` (126), duplicate locator and converter (312, 313), eager `AssetRedirects` (482-484)                           | Phase 0 unchanged                                               |
| F13 Docking statics                            | `Dockable.Factory` static registry still present                                                                                                                                                                        | Phase 5 unchanged                                               |

Tooling facts the spec does not mention, which shape every new UI project:

- Views are paired with view models by `[ViewModel(typeof(...))]`, consumed by
  `Mvvm.Generators`. Every new UI project carries the generator and attribute
  references that `Oxygen.Editor.WorldEditor.csproj` has today.
- `Themes/Generic.xaml` is one dictionary for the whole monolith. Each editor
  project gets its own `Themes/Generic.xaml`, merged by the application through
  the editor's module (spec risk table, last rows).
- The application has no test project, so Phase 0 and the composition-root
  work of Phase 1 step 5 are verified by the reviewer's editor build.

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
   difference from `Oxygen.Managed.Assets`: editor asset services (catalog,
   identity reducer, picker, asset messages) over the content pipeline, versus
   reusable asset and cook primitives.
3. **Phase 1 module discovery.** Until Phase 2 introduces scopes, each editor
   exposes a module object. The application registers the module instances at
   the root; `WorkspaceViewModel.OnSetupChildContainer` runs every module's
   window-layer registration into the existing child. This removes the
   workspace's knowledge of editor types (C10, R4) without changing resolution
   behavior, and Phase 2 swaps the target container, not the modules.
4. **Contracts are written once.** As the spec requires, new contracts land in
   their target shape in Phase 1 step 2, behind adapters over today's
   implementations. A later phase that needs to change a contract stops and
   raises it.
5. **Test placement.** Each new project follows the canonical shape
   (`README.md`, `src`, `tests`). Tests move in the same commit as the code they
   cover; `InternalsVisibleTo` moves with them (R6).

## 3. Placement of what the spec does not place

| Code                                                                                                                                 | Target                   | Cut                                                                                                                                                         |
| ------------------------------------------------------------------------------------------------------------------------------------ | ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IPlayWorkspace`, `IRuntimeProblemSource`, `PlayRequest`, `PlayStatus*`, `PlayPanel`, `PlayActivity`, `PlayFailure`                  | `Authoring.Abstractions` | **C16**: `WorkspaceViewModel` keeps implementing `IPlayWorkspace` (as with `ICookingWorkspaceActions`, C12); Problems consumes `IRuntimeProblemSource` only |
| `PlaySessionService`, `PlaySessionSettingsService`, `PlayerConnection`, `PlayerSettingsSync`, `RuntimeProblemReader` and their tests | `Authoring`              | Registered by `AuthoringModule`, not by `WorkspaceViewModel.Play`                                                                                           |
| `PlayTransportBar`, `ScenePlayToolbarViewModel`, `PlayerWindowModeIndexConverter`, `PlayerWindowSizeOption`, `PlayPrimaryAccent`     | `SceneEditor`            | Moves with the scene editor (`SceneEditorView` hosts the bar)                                                                                               |
| `Diagnostics/SceneOperationResults`, `RuntimeOperationResults`                                                                       | `Authoring.Abstractions` | Operation-result vocabulary used by several editors                                                                                                         |
| `Utils/ThumbnailGenerator`, `ThumbnailTemplateSelector`                                                                              | `SceneExplorer`          | As the spec lists for the explorer's thumbnails                                                                                                             |

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

- **S1.1 Assets** (C8). Create `Oxygen.Editor.Assets` from the Content
  Browser's `AssetIdentity`, `Infrastructure`, `Materials`, `Messages`,
  `Models` and non-UI `Scripts`; `CodeEditorService` gets an interface in
  `Assets`. Move their tests to `Assets.Tests`. Content Browser, World Editor
  and Material Editor reference `Assets`. Gate: `Assets.Tests`, Content Browser
  tests.
- **S1.2 Contracts.** Create `Authoring.Abstractions` and `Shell`, then, as
  separate commits:
  1. Pure moves: C1, C2 (messages), C4 (with the two Runtime types), C5, C6,
     C13, C16 contracts, `Diagnostics`; `WindowPlacement` in
     `Oxygen.Editor.Documents`.
  2. C7: preference interfaces and value records, constructors take the
     interface.
  3. C3: organizer split into the model-level contract and the explorer-side
     adapter refresh.
  4. C15 and the constructor audit: command and content-demand services lose
     their `WindowId`; requests carry `WindowPlacement`. The audit list and the
     message classification table (window bus, session event, service call)
     are recorded in the spec's section 6.11 as decisions.
  5. `Shell` contracts: `PanelDescriptor`, editor descriptors,
     `IDocumentEditorProvider` (C9, C10).
     Gate: World Editor `Unit` and `Unit.UI` tests that cover the moved types.
- **S1.3 Authoring** (C11). Create `Authoring` with the command service,
  selection, mutator, organizer model, preferences, scripting, live engine
  sync, Play services and `RuntimeSessionService` (engine start, mounts, cooked
  roots taken out of `WorkspaceViewModel`). Create `Authoring.Tests` and
  `Authoring.TestSupport` from `tests/Unit/Documents`, `Services`, `Scripting`,
  `Play` and `testsupport/Authoring`, `Native`. Gate: `Authoring.Tests` passes
  under `traverse Invoke-Tests` without the WinUI host.
- **S1.4 Editors**, one commit each with module, theme dictionary and tests, in
  this order: `Output`, `Problems`, `Cooking` (C12), `CookedInspection` (C9),
  `SceneExplorer` (C3 explorer side), `SceneEditor` (C14, C9, Play UI),
  `Inspector`; then `MaterialEditorModule` registers its editor provider (C9).
  After each, the workspace stops registering that editor's types and the
  document host stops constructing it. Gate: that editor's tests; UI test
  failures go to the reviewer.
- **S1.5 Host.** `WorldEditor` is reduced to the workspace host (R4); the
  application composes the modules (decision 3). Gate: `WorldEditor` tests and
  the reviewer's editor build.
- **S1.6 Enforcement.** Project-graph check for R1 to R5 as a `unittest` in
  `tooling/tests`; delete the emptied monolithic test projects; hand-built test
  containers move to the shared host-equivalent builder in `TestHelpers`. Gate:
  tooling tests.

### Phase 2: scope factory and topology test

- **S2.1** `DroidNet.Composition`: `ScopeFactory`, `ScopeHandle`, module and
  activation-list contracts, `RouterScopeModule`, with the spec 6.3 rules and
  their own tests (chain root > session > window > panel, optional
  dependencies, open generics, disposal).
- **S2.2** Topology test over the shared builder, with the reviewed
  `RegisterDelegate` allowlist.
- **S2.3** Workspace and Content Browser children move onto the factory; the
  three project-bound preference services and the keyed settings manager move
  to the window module; `DockPanelViewModel`/`DockPanel` move to the window
  module; eager-resolve workarounds deleted; `WorkspaceViewModel` resolves only
  through its handle. Gate: topology and project-switch tests, reviewer's
  editor build.

### Phase 3: registration modules

- **S3.1** Split `Program.cs` into the Editor modules of spec 6.7, one
  subsystem per commit; eager resolves become activation lists. Gate: topology
  test, reviewer's editor build.

### Phase 4: Project Session

- **S4.1** `IProjectSessionHost`, `IProjectSession`, `ISessionEvents`,
  `CookPublicationRecovery` split, `IDocumentHistory`, session clipboard, and
  the reworked `ProjectActivationCoordinator` with its cancel, replace and
  navigation-failure contract.
- **S4.2** Clusters, one commit each: preferences, assets, content pipeline and
  cooking, scene stack, script tooling, Play. Each injects `ProjectContext`,
  deletes its `ProjectChanged` reset and uses the session lifetime token.
- **S4.3** Remove `CurrentProject` and delete `IProjectContextService`.
- Gate: the spec's Phase 4 tests, including the **open check** of the issue:
  close the last workspace window with the Project Browser open and assert the
  process stays up. If WinAppSDK ends the process, stop and raise the shutdown
  policy before continuing.

### Phase 5: Docking, windows, placement, panels

- **S5.1** Docking per `IDocker` (registries, id counter, messenger), no
  `Dockable.FromId` in editor code, dynamic workspace window targets. Docking
  library tests first.
- **S5.2** `IRetainedViewModel` in Routing, Aura and Router, with tests.
- **S5.3** Window scope factory, window registry, placement service, layout
  store; `DocumentManager` receives resolved requests.
- **S5.4** Panel registry and `IPanelHost`, then the six panels one commit each,
  Content Browser first.
- **S5.5** Floating-window host proving the move contract.
- Gate: the spec's Phase 5 tests; the reviewer validates a second window and a
  moved panel in the editor. Visible UI stays uncommitted until the reviewer
  accepts it.

### Phase 6: service location and static messengers

- **S6.1** Remove `IContainer` from `SceneEditorViewModel`,
  `WorkspaceViewModel` and `ContentBrowserViewModel`, and the `ReturnDefault`
  paths. **S6.2** Replace `WeakReferenceMessenger.Default` and
  `StrongReferenceMessenger.Default`. **S6.3** Apply the disposal policy to
  every registration. Gate: topology test.

### Phase 7: documentation

- Replace the spec with the final topology reference and update
  `lld/project-workspace-shell.md` and `ARCHITECTURE.md` section 8.1.
  Section 2 decision 1 has already kept the layout documents current.

## 5. Execution risks beyond the spec

| Risk                                                                                    | Handling                                                                                                                  |
| --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Concurrent editor work on the `editor` branch touches files being moved                 | Move one folder per commit and commit promptly; a move never carries unrelated edits                                      |
| `x:Bind` and `ms-appx` paths break silently after a XAML move                           | Rewrite resource URIs in the moving commit; each editor's UI tests load its views (spec risk table)                       |
| A moved type turns public and collides with an existing name in its new namespace scope | Namespaces are preserved (R6), so collisions surface at compile time in the moving commit                                 |
| An editor build of the application is the only check of composition-root changes        | Phase 0, S1.5, S2.3, S3.1 and Phase 4 end with the reviewer's build; no further slice starts on a broken composition root |

## 6. Status ledger

Progress is tracked in the issue's phase checklist. This plan is updated when a
slice lands only if the slice changes the plan.
