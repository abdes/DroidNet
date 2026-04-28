# ED-M01 Project Browser And Workspace Activation

Status: `validated - template layout superseded by ED-M06A`

Planning note:

- ED-M01 remains the project browser, activation, validation, and workspace
  transition milestone.
- Concrete predefined template payloads, game project folder skeletons,
  starter scene paths, default authoring creation targets, and Content Browser
  root behavior are superseded by
  [ED-M06A-game-project-layout-and-template-standardization.md](./ED-M06A-game-project-layout-and-template-standardization.md).
- Any future work touching project creation must follow ED-M06A and
  `project-layout-and-templates.md` for layout details.

## 1. Purpose

Implement the first real editor workflow: launch into Project Browser, create
or open a project, classify invalid projects, activate a workspace only after a
valid project context exists, and surface visible operation results for
failures and partial restoration.

This milestone exists so later authoring milestones can assume a real active
project/workspace boundary instead of relying on startup side effects, mutable
global project state, or log-only failures.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-001` | Establish the V0.1 end-to-end editor entry workflow. |
| `GOAL-006` | Make project and workspace failures visible and actionable. |
| `REQ-001` | Normal launch starts at Project Browser. |
| `REQ-002` | Recent/open/create project workflows handle valid and invalid projects. |
| `REQ-003` | Workspace restoration is best effort and visibly partial on failure. |
| `REQ-022` | Project open/create failures produce visible operation results. |
| `REQ-024` | Failure causes are classified instead of being log-only. |
| `SUCCESS-001` | A project can be opened or created and the editor workspace appears. |

## 3. Required LLDs

Only these LLDs gate ED-M01:

- [project-workspace-shell.md](../lld/project-workspace-shell.md)
- [project-services.md](../lld/project-services.md)
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)

`runtime-integration.md` is not an ED-M01 LLD. ED-M01 may remove the current
startup dependency on `IEngineService`, but runtime lifecycle design remains
owned by ED-M02.

## 4. Scope

ED-M01 includes:

- Project Browser is the first visible route on normal launch.
- Project Browser launch path does not inject, reference, or transitively depend
  on `IEngineService`.
- Native runtime directory discovery remains process-bootstrap infrastructure.
- Project open/create requests flow through a shell activation coordinator.
- Existing Project Browser boolean open/create APIs are replaced at the
  activation boundary.
- Project services classify project folder, manifest, schema, and content-root
  failures.
- A read-only `ProjectContext` snapshot is created only after successful
  validation/loading.
- All `CurrentProject` consumers, including workspace activation, Content
  Browser, project asset catalog, and any other cross-project reader, move to
  `IProjectContextService`. `IProjectManagerService` is no longer a public
  cross-project contract after ED-M01.
- Project creation writes a V0.1 manifest with `SchemaVersion = 1`.
- Recent project usage updates only after successful activation.
- Stale recent projects remain visible until retry, browse to a new location,
  or explicit removal.
- Workspace activation runs only after project context commit.
- Workspace restoration is best effort and produces partial-failure diagnostics.
- Operation results and diagnostics exist as shared contracts, with a host-level
  in-memory store and output-log adapter.
- Project Browser open/create/invalid states show visible results.

## 5. Non-Scope

ED-M01 does not include:

- Live engine viewport stabilization or runtime surface/view lifecycle.
- Cook, import, mount, pak, or standalone validation workflows.
- Scene document command model, dirty state, undo/redo, or save semantics.
- Material editor or physics editor behavior.
- Editable project content roots or project settings UI.
- Workspace multi-project support.
- Replacing the legacy scene-save cook side effect, except that new ED-M01 APIs
  must not expand or depend on it.

## 6. Implementation Sequence

### ED-M01.1 - Diagnostics Contracts And Host Store

Create the shared result model before replacing project workflows so every new
service can return typed results from the start.

Tasks:

- Add operation-result and diagnostic contract types in `Oxygen.Core`.
- Add `OperationResult`, `DiagnosticRecord`, `FailureDomain`,
  `AffectedScope`, `PrimaryAction`, status/severity enums, and stable
  diagnostic-code conventions.
- Add the ED-M01 diagnostic code prefix table, including `OXE.PROJECT.*` and
  `OXE.WORKSPACE.*`, in `Oxygen.Core`.
- Add `IStatusReducer` and exception-adapter behavior, including
  `OperationCanceledException -> Cancelled`.
- Add `IOperationResultPublisher` with `IObservable<OperationResult>`
  semantics.
- Add `IOperationResultStore` with current-session snapshot and scope-filter
  behavior.
- Add host-level result store/publisher composition in `Oxygen.Editor`.
- Add output-log adapter that writes result summary entries to
  `OutputLogBuffer` with `OperationId`.
- Use `LogContext.PushProperty("OperationId", id)` at workflow boundaries where
  Serilog is available.

Required behavior:

- Result contracts do not depend on WinUI, feature UI projects, runtime interop,
  or native types.
- Result summary log entries may persist as logs, but the operation-result store
  remains in-memory session state.
- `Unknown` is available as an ED-M01 failure domain fallback.

Validation:

- Unit tests cover status reduction, cancellation mapping, exception mapping,
  publisher observable behavior, and result-store snapshot/filter behavior
  where straightforward.

### ED-M01.2 - Engine Startup Decoupling

Remove the current runtime dependency from no-project startup before Project
Browser workflows are migrated, so the rest of ED-M01 is implemented against the
correct startup baseline.

Tasks:

- Remove `IEngineService` injection from `App`.
- Remove `EnsureEngineIsReady()` from `App.OnLaunched`.
- Keep `NativeRuntimeLoader.RegisterEngineRuntimeDirectory()` in
  `Program.Main` as process-bootstrap native DLL discovery.
- Ensure Project Browser startup route can complete if the embedded engine
  cannot initialize.
- Leave actual engine startup to ED-M02 runtime readiness services, or to a
  workspace/runtime request after project context exists.

Required behavior:

- Normal launch to `/pb/home` does not initialize or start the embedded engine.
- Project Browser launch path does not transitively depend on `IEngineService`.

Validation:

- Startup can be exercised with the runtime service unavailable or failing
  without preventing Project Browser from appearing.

### ED-M01.3 - Project Validation And Context Services

Build the new project service layer before wiring UI to it.

Tasks:

- Add `ProjectValidationResult` with states `Valid`, `Missing`,
  `NotAProject`, `InvalidManifest`, `UnsupportedVersion`, `Inaccessible`, and
  `InvalidContentRoots`.
- Add V0.1 manifest contract with required `SchemaVersion = 1`.
- Move `Location` and `LastUsedOn` off the persisted `Project.oxy` payload into
  runtime/persistent-state models. Update `ProjectInfo` serialization and
  round-trip tests accordingly.
- Update in-repo sample and test-fixture `Project.oxy` files to include
  `SchemaVersion = 1`, explicit `AuthoringMounts`, and no serialized
  `Location` or `LastUsedOn`.
- Remove `EnsureDefaultAuthoringMounts` auto-heal behavior from validation
  paths; missing/invalid roots must surface as `InvalidContentRoots`.
- Add path normalization using `StringComparer.OrdinalIgnoreCase` for V0.1.
- Add `ProjectContext` as a read-only snapshot, not a live `IProject`
  reference.
- Add `IProjectContextService` with read-only `ActiveProject`,
  `IObservable<ProjectContext?>` replay/current-value semantics, activation,
  replacement, close, and unload.
- Add `ProjectCookScopeProvider` with the ED-M01 minimal scope: project
  identity, project root, cooked output root.
- Add Project Creation Service that creates a complete V0.1 manifest and
  validates the result before activation.
- Add Recent Project Adapter that updates usage only after activation succeeds
  and classifies stale records without deleting them implicitly.

Required behavior:

- Project services do not start the engine, mount roots, call native interop,
  import, cook, or write cooked indexes.
- `IProjectManagerService` is no longer the public active-project service
  contract. Any remaining implementation is internal persistence or migration
  code.
- Scene/node/component mutations do not publish through
  `IProjectContextService`; later authoring milestones own that channel.

Validation:

- Unit tests cover validation states, unsupported schema version, invalid
  content roots, active-context replacement, close/unload, replay notification,
  recent-update ordering, and cooked-root policy.

### ED-M01.4 - Shell Activation Coordinator

Introduce the single workflow owner for open/create activation.

Tasks:

- Add shell activation coordinator owned by `Oxygen.Editor`.
- Define `ProjectActivationRequest` with `Mode` (`OpenExisting` or
  `CreateFromTemplate`), `CorrelationId`, `RequestedAt`, `SourceSurface`
  (`Home`, `Open`, or `New`), open-existing fields (`ProjectLocation`, optional
  `RecentEntryId`), and create-from-template fields (`TemplateId` or template
  location, `ProjectName`, `ParentLocation`).
- Define `ProjectActivationContext` as a shell-side wrapper that references the
  project-services-owned `ProjectContext` and exposes only project identity,
  display name, project root, usage record identity, project context handle, and
  workspace restoration request. It must not copy authoring mounts or
  content-root policy.
- Route open-existing and create-from-template through the coordinator.
- Ensure create-from-template performs create, validate, activate, recent
  update, and workspace transition as one operation.
- Ensure the coordinator emits one top-level operation result with child
  diagnostics for request validation, project service work, recent usage,
  workspace navigation, and workspace restoration.
- Add cancellation handling:
  - cancellation before project context commit leaves active state unchanged.
  - cancellation after workspace window creation closes the partial window and
    emits `Cancelled` where possible.
- Enforce one activation attempt at a time per host process.

Required behavior:

- Recent project/template usage updates are coordinator-owned and occur only
  after successful project context creation.
- Project Browser view models do not navigate directly to `/we`.
- Activation failure keeps Project Browser usable.

Validation:

- Unit tests cover stage ordering, create-then-activate, recent-update guarding,
  cancellation, failure mapping, and half-created workspace cleanup where
  test seams exist.

### ED-M01.5 - Project Browser Service And UI Migration

Narrow Project Browser to UI and support services.

Tasks:

- Remove `IProjectBrowserService.OpenProjectAsync(IProjectInfo)`,
  `IProjectBrowserService.OpenProjectAsync(string)`, and
  `IProjectBrowserService.NewProjectFromTemplate(...)`. Do not introduce
  coordinator-delegating shims; view models call the shell activation
  coordinator directly.
- Keep Project Browser support responsibilities: known locations, templates,
  quick-save locations, Project Browser settings, and recent-project
  presentation.
- Update `HomeViewModel`, `OpenProjectViewModel`, `NewProjectViewModel`,
  `RecentProjectsListViewModel`, and related views/dialogs to call the shell
  activation coordinator.
- Add inline operation result presentation for invalid recent projects,
  create/open failures, and retry/browse/remove/details actions.
- Preserve stale recent project rows until explicit removal.

Required behavior:

- Project Browser does not depend on WorldEditor internals.
- Project Browser does not know workspace activation internals beyond the
  coordinator contract.
- Project Browser stays responsive during async validation/open/create.

Validation:

- Project Browser can show missing project, not-a-project, invalid manifest,
  unsupported version, inaccessible path, invalid content roots, and template
  copy failures without parsing logs.

### ED-M01.6 - Workspace Activation And Restoration

Move workspace creation behind committed project context.

Tasks:

- Add workspace activation adapter owned by the shell/workspace boundary.
- Pass project activation context to `WorkspaceViewModel` through navigation
  state or a host service; do not infer the active project from a mutable global
  before context commit.
- Update `WorkspaceViewModel` initialization so child container, initial dock
  route, and workspace composition run after activation context is available.
- Migrate workspace activation consumers, Content Browser consumers, project
  asset catalog consumers, and any other cross-project readers from
  `IProjectManagerService.CurrentProject` to `IProjectContextService`.
- Keep scene-save and document-authoring behavior out of scope except where
  constructor dependencies must change to avoid `CurrentProject`.
- Add restoration adapter for dock layout, last opened document/scene, and
  content browser state using persisted state from `Oxygen.Editor.Data`.
  Failure to apply persisted restore data is reported as `Partial` or `Failed`
  per restoration result classification, never silently dropped.
- Treat dock-layout failure as partial if default workspace can be shown.
- Close the workspace window if activation creates a window and then determines
  no usable workspace shell can be shown.

Required behavior:

- Workspace opens only after project context commit.
- Partial restoration failure is visible and non-fatal.
- Failed workspace activation closes partial windows and reports back to Project
  Browser.

Validation:

- Valid recent/open/create project reaches workspace.
- Missing last scene or invalid layout reports partial restore without blocking
  usable workspace entry.

### ED-M01.7 - Dependency Cleanup And Tests

Finish the milestone by removing forbidden references and locking tests around
the new seams.

Tasks:

- Remove direct Project Browser dependency on WorldEditor internals.
- Remove direct Project Browser activation dependency on `IProjectManagerService`.
- Replace `Program.ConfigureApplicationServices` registrations for public
  current-project access with `IProjectContextService`.
- Keep legacy scene persistence/cook APIs only where needed by later milestones,
  not as the ED-M01 active-project contract.
- Add or update tests in:
  - `Oxygen.Core.Tests` for result contracts/reducers where needed.
  - `Oxygen.Editor.Projects.Tests` for validation/context/cook-scope policy.
  - `Oxygen.Editor.Tests` for host activation coordinator behavior. If creating
    a host test project proves impractical, keep the coordinator service-shaped
    and test the pure coordinator contract through a non-UI test seam while
    excluding WinUI host wiring.
  - UI tests only for high-value Project Browser route/result behavior that the
    existing WinUI test infrastructure can cover without fragility.

Validation:

- MSBuild for touched projects through the repository build configuration.
- Targeted test projects for changed shared contracts and project services.
- Manual/editor run validation for startup and workspace transition if UI
  automation is not yet practical.

## 7. Project/File Touch Points

Expected primary touch points:

- `projects/Oxygen.Core/src/`
  - operation-result and diagnostic contract types.
- `projects/Oxygen.Core/tests/`
  - status reducer and exception adapter tests, if contracts include behavior.
- `projects/Oxygen.Editor/src/App.xaml.cs`
  - remove `IEngineService` injection and `EnsureEngineIsReady()` launch call.
- `projects/Oxygen.Editor/src/Program.cs`
  - DI changes for activation coordinator, result store/publisher,
    `IProjectContextService`, and Project Browser service narrowing.
  - host project file: `projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj`.
- `projects/Oxygen.Editor.ProjectBrowser/src/`
  - narrow `IProjectBrowserService`.
  - route view models through shell activation coordinator.
  - add inline result states.
- `projects/Oxygen.Editor.Projects/src/`
  - project validation, context, creation, recent adapter, cook-scope provider.
  - migration away from public `IProjectManagerService.CurrentProject`.
- `projects/Oxygen.Editor.Projects/tests/`
  - validation/context/recent/cook-scope tests.
- `projects/Oxygen.Editor.WorldEditor/src/Workspace/`
  - workspace activation context consumption and restoration behavior.
- `projects/Oxygen.Editor.WorldEditor/src/Output/`
  - consume adapted operation-result summary entries through existing output
    console infrastructure.
- `projects/Oxygen.Editor.Data/src/`
  - recent project and template usage integration only if existing services
    need small API support for stale entries or guarded updates.

Expected project/reference changes:

- `Oxygen.Editor.ProjectBrowser` must not reference `Oxygen.Editor.WorldEditor`.
- Shared diagnostics contracts belong to `Oxygen.Core`.
- Host diagnostics services are composed in `Oxygen.Editor`.
- Feature projects consume diagnostics contracts without depending on host UI
  implementation.

## 8. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| Existing `Project.oxy` files predate `SchemaVersion = 1` and explicit `AuthoringMounts`, so removing `EnsureDefaultAuthoringMounts` auto-heal will reclassify them as `UnsupportedVersion` or `InvalidContentRoots`. | ED-M01.3 includes a one-time fixture/sample sweep that rewrites in-repo manifests to the V0.1 shape. Manifests authored outside the repo are out of scope and must be recreated or fixed through Project Browser invalid-state recovery. |
| Project Browser open/create is currently mixed with project persistence and recent usage. | Introduce shell activation coordinator first, then move Project Browser view models one workflow at a time. |
| `IProjectManagerService.CurrentProject` is used by workspace, Content Browser, and project asset catalog consumers. | Add `IProjectContextService` and migrate all cross-project current-project reads before removing the public active-project contract. |
| Scene save APIs still live on legacy project manager. | Keep scene save/cook replacement out of ED-M01; do not route new activation work through those APIs. |
| Removing engine startup from `App.OnLaunched` may hide runtime failures until later. | That is intentional; ED-M01 records Project Browser startup success separately from runtime readiness. ED-M02 owns runtime failure surfaces. |
| Partial workspace creation can leave a broken window. | Centralize workspace activation and make close-on-failure an explicit coordinator responsibility. |
| Output-log summary entries could be mistaken for persistent result state. | Keep `IOperationResultStore` in-memory and document adapted log entries as non-authoritative logs. |
| WinUI UI automation may be expensive early. | Prefer service/coordinator tests first; add UI tests only for stable startup/result surfaces. |

## 9. Validation Gates

ED-M01 can close only when:

- Normal launch opens Project Browser first.
- Project Browser launch does not initialize/start the embedded engine and does
  not inject/reference `IEngineService`.
- Recent project list loads without blocking the UI.
- Missing recent project remains visible and actionable.
- Invalid project states are visible without log inspection.
- Opening a valid project by recent entry reaches the workspace.
- Opening a valid project by folder reaches the workspace.
- Creating a valid project from template reaches the workspace.
- Failed open/create stays in Project Browser and shows an operation result.
- Successful activation updates recent usage after project context commit.
- Workspace activation happens only after project context commit.
- Partial workspace restoration is visible and non-fatal.
- Failed workspace activation closes any partially created workspace window.
- Project activation logs correlate to the operation result through
  `OperationId`.
- Required targeted builds/tests pass or any skipped verification is recorded in
  the ED-M01 validation ledger.

## 10. Status Ledger Hook

When ED-M01 implementation lands, update:

- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) ED-M01 checklist.
- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) validation ledger
  with one concise ED-M01 evidence row.

The ledger evidence should record:

- startup path.
- Project Browser engine independence.
- valid project open/create behavior.
- invalid project behavior.
- workspace activation behavior.
- partial restoration behavior.
- operation-result/log-correlation behavior.
- build/test commands used for validation.
