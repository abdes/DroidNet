# Oxygen Editor Project Layout

Status: `authoritative layout guide`

This document is the placement authority for Oxygen Editor code and design
artifacts. Its purpose is to make ownership obvious before code is written.

The rule of thumb is simple:

> Put code where the state lives. Put UI where the user edits that state. Put
> engine calls behind runtime services. Do not let convenience references define
> architecture.

Placement is decided by three questions:

1. **What product does this own?** Scene, material, physics sidecar, project,
   asset catalog, settings, runtime session, or reusable editor UI.
2. **Which layer is it?** Authoring data, editor UI, tooling orchestration,
   runtime application, native bridge, or storage primitive.
3. **Who else should depend on it?** If several editor features need it, it
   cannot live inside one feature's UI project.

## 1. Design Package

```text
design/editor/
+-- README.md
+-- PRD.md
+-- ARCHITECTURE.md
+-- DESIGN.md
+-- PLAN.md
+-- PROJECT-LAYOUT.md
+-- IMPLEMENTATION_STATUS.md
+-- lld/
+-- plan/
```

Project-local docs may remain near the owning project, but durable
cross-cutting editor architecture belongs here. Shared engineering rules live in
[design/oxygen/RULES.md](../oxygen/RULES.md).

## 2. Canonical Project Shape

Every .NET editor project uses the DroidNet project shape:

```text
projects/<ProjectName>/
+-- README.md                # mandatory project contract
+-- src/                     # exactly one production project by default
|   +-- <ProjectName>.csproj
+-- tests/                   # test projects owned by this project
|   +-- <ProjectName>.Tests.csproj
+-- docs/                    # project-local design notes, migration notes
+-- samples/                 # project-local samples, when useful
```

`README.md` is mandatory because the repo build imports it into package
metadata when the project is packable. `docs/` and `samples/` may be empty or
absent until needed, but those names are reserved. New project-local design
documents go under `docs/`; cross-cutting editor architecture goes under
`design/editor`.

Nested support projects follow the same shape under their owning project only
when they are independently buildable products, for example:

```text
projects/Oxygen.Editor.Data/Generators/
+-- README.md
+-- src/
+-- tests/
```

Do not create ad hoc roots such as `Source`, `Tests`, `Documentation`,
`Examples`, or `Playground`.

## 3. MSBuild Project Contract

Editor `.csproj` files are thin SDK-style project files. Shared build behavior
comes from the repository MSBuild hierarchy:

- `Common.props`
- `Directory.build.props`
- `Directory.build.targets`
- `Directory.packages.props`

Project files must follow these rules:

1. Use SDK-style projects: `<Project Sdk="Microsoft.NET.Sdk">`.
2. Use `TargetFrameworks`, not `TargetFramework`, even for a single target.
3. Use central package management. `PackageReference` entries do not specify
   versions; versions belong in `Directory.packages.props`.
4. Use `$(ProjectsRoot)` for repository project references.
5. Do not override `OutputPath`, `IntermediateOutputPath`, package output, or
   artifacts layout in project files. The repo-level artifacts layout owns
   build output.
6. Do not add project-local analyzer/style/package boilerplate already supplied
   by the root MSBuild files.
7. Test projects use the `.Tests` suffix. WinUI UI test projects use
   `.UI.Tests`; the shared MSBuild files attach the correct test behavior from
   the name.
8. WinUI projects set `<UseWinUI>true</UseWinUI>` and reference WinUI packages
   through central package versions.
9. Project files may set project identity metadata such as `Title`,
   `Description`, `PackageTags`, `RootNamespace`, `AssemblyName`, and
   `AllowUnsafeBlocks` when needed by the project.
10. Do not introduce project-local `Directory.Build.props` or
    `Directory.Build.targets` without documenting why the repo-level build
    contract cannot cover the need.

The build contract is part of the architecture. A project that needs a special
build exception must document the exception in its project README and, when it
affects editor architecture, in `design/editor`.

## 4. Layers

Editor projects are organized by ownership layer:

```text
Shell / composition
  -> editor UI kit
    -> feature editors
      -> editor tooling services
        -> authoring domain + asset/project models
          -> persistence/storage primitives

Runtime integration is a side boundary:
feature UI -> runtime services -> interop -> Oxygen Engine
```

Dependency direction flows downward or through the runtime boundary. When a
higher layer needs behavior from another feature, introduce a small service
contract at the owning layer instead of reaching across UI internals.

## 5. Project Ownership

Some owners listed here are target projects that may not exist yet. If new work
falls under a target owner, create the project when the new-project rule in
section 12 is satisfied; do not hide the work in the app or an unrelated
feature project.

| Project                                  | Owns                                                                                                                                                                                                | Must Not Own                                                                                                            |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `projects/Oxygen.Editor`                 | App entry point, bootstrap, settings initialization, the WinUI application, native runtime bootstrap; the integration suite of the modules together.                                                | Service registrations, scene editing policy, asset workflow policy, cooker policy, engine operations beyond bootstrap.  |
| `projects/Oxygen.Editor.Composition`     | The composition root: the editor scope's registrations, top-level routes and windows, the scene workspace window, its scope's registrations; the topology tests of the scope chain.                 | Scene editing policy, asset workflow policy, cooker policy, engine operations. Only the application host references it. |
| `projects/Oxygen.Editor.UI`              | Oxygen-specific reusable editor widgets, field controls, overlays, editor styles, and UI helper view models that are not general DroidNet controls.                                                 | Feature policy, domain models, project/cook/runtime behavior.                                                           |
| `projects/Oxygen.Editor.Shell`           | Application frame: the document service, document areas and tabs, close prompts, histories, command routing, placement, panel and editor contributions, the docking workspace, desktop integration. | Scene, asset or pipeline policy; references above foundation.                                                           |
| `projects/Oxygen.Editor.Routing`         | Editor-specific route helpers and route integration glue.                                                                                                                                           | Feature state or feature workflow policy.                                                                               |
| `projects/Oxygen.Editor.ProjectExplorer` | No-project startup experience, project creation/opening UI, recent project UI, project templates UI.                                                                                                | Project persistence rules, scene editing, content browsing inside an opened workspace.                                  |
| `projects/Oxygen.Editor.Documents`       | Generic document abstractions shared by editor features.                                                                                                                                            | World-editor-specific scene document behavior.                                                                          |
| `projects/Oxygen.Editor.Schemas`         | Schema/overlay catalog, typed properties/edits, mixed values, snapshots, apply/session mechanics.                                                                                                   | Feature UI/policy, runtime lifecycle, native calls, cooked binary offsets.                                              |
| `projects/Oxygen.Editor.World`           | Pure authoring scene/domain model: scenes, nodes, components, scene serialization, scene-owned settings, authoring references.                                                                      | WinUI, routing, docking, runtime engine handles, interop, cook execution.                                               |
| `projects/Oxygen.Editor.MaterialEditor`  | Full material document/editor workspace, material inspector/tools, material preview UI, material validation presentation.                                                                           | Reusable asset/cook primitives, native engine calls, project policy.                                                    |
| `projects/Oxygen.Editor.SceneEditor`     | The scene editor: viewports, cameras, transform gizmos and tools, the toolbar and the Play bar; the preview, viewport and transform tool settings.                                                  | Applying authoring state to the engine, Play sessions, native calls (it reaches the engine through Runtime only).       |
| `projects/Oxygen.Editor.SceneInspector`  | The Properties panel of a scene: the selected nodes' components, the scene's environment and the script slots.                                                                                      | Scene edit rules, the shared property controls, engine calls.                                                           |
| `projects/Oxygen.Editor.SceneExplorer`   | The Scene Explorer panel: the tree of the active scene's nodes and explorer folders, where nodes are created, renamed, moved, grouped, hidden, locked and selected.                                 | Scene edit rules and the shared selection.                                                                              |
| `projects/Oxygen.Editor.CookedInspector` | The Cooked Inspector panel: a read-only report of the cooked output of a scope, and the information of the asset it inspects.                                                                       | Reading and validating cooked output.                                                                                   |
| `projects/Oxygen.Editor.Cooking`         | The Cooking panel: the cook runs of the project with their issues, output and assets; saving what a cook reads, and showing where its issues and outputs are.                                       | Cook orchestration.                                                                                                     |
| `projects/Oxygen.Editor.Output`          | The Output panel: the log view over the editor's output buffer, and the player's output in it.                                                                                                      | Log production.                                                                                                         |
| `projects/Oxygen.Editor.Problems`        | The Problems panel: the problems of every asset family and of the running scene, grouped by file; selecting a runtime problem's node; coming forward on a runtime error.                            | Problem production.                                                                                                     |
| `projects/Oxygen.Editor.Physics`         | Shared physics authoring domain objects used by physics scenes and scene-attached physics components.                                                                                               | WinUI, editor documents, runtime engine handles, native interop.                                                        |
| `projects/Oxygen.Editor.PhysicsEditor`   | Full physics scene sidecar editor workspace, physics scene documents/tools, physics validation presentation.                                                                                        | Shared physics domain ownership, reusable cook primitives, native engine calls, project policy.                         |
| `projects/Oxygen.Editor.ContentExplorer` | The Content Explorer panel: the project's content folders and mounts, and the assets of the folders it shows, with their search, filters, layouts, details and import dialogs.                      | Relocation and creation logic (ContentPipeline), the asset pickers (Pickers), engine mounting.                          |
| `projects/Oxygen.Editor.ContentPipeline` | Editor tooling orchestration for import, cook, pak, inspect, asset jobs, pipeline diagnostics, and engine cooker/content tool adapters.                                                             | Panels, feature-specific authoring policy, low-level cooked binary structures.                                          |
| `projects/Oxygen.Editor.Projects`        | Project metadata, services, settings, content root policy, project-level cook scope facts.                                                                                                          | WinUI panels, scene inspector UI, native interop, cook execution/orchestration.                                         |
| `projects/Oxygen.Editor.Runtime`         | Managed engine lifecycle, effective runtime settings application, surface leases, view service, cooked-root mount service, runtime diagnostics.                                                     | Authoring defaults, project policy, UI workflow, scene serialization.                                                   |
| `projects/Oxygen.Editor.Interop`         | C++/CLI bridge to stable Oxygen Engine APIs.                                                                                                                                                        | Authoring policy, UI behavior, project layout policy, cooker policy, fallback behavior.                                 |
| `projects/Oxygen.Editor.Data`            | Durable editor data, settings infrastructure, persistent state DB, settings descriptors/generators.                                                                                                 | Feature-specific settings ownership or UI.                                                                              |
| `projects/Oxygen.Managed.Core`           | Shared non-UI editor contracts and utilities needed across multiple editor modules, including operation-result and diagnostic contract types.                                                       | Feature UI, workflow orchestration, persistence, runtime services, native interop.                                      |
| `projects/Oxygen.Managed.Assets`         | Managed asset identities, references, catalogs, import/cook primitives, loose cooked index utilities.                                                                                               | Editor UI, project workflow policy, live engine mounting.                                                               |
| `projects/DroidNet.Storage`              | Storage abstractions and native filesystem implementation.                                                                                                                                          | Asset semantics, editor settings, project policy.                                                                       |

## 6. Dependency Rules

1. Domain projects must not depend on UI projects.
   `Oxygen.Editor.World`, `Oxygen.Managed.Assets`, and `DroidNet.Storage` stay free of
   WinUI, routing, docking, runtime, and interop.
2. Feature UI may depend on domain, tooling, and service projects, but feature
   UI must not depend on another feature UI's internals. Reusable
   Oxygen-specific widgets move to `Oxygen.Editor.UI`; reusable behavior moves
   to the owning service/model project or an explicit feature contract.
3. `Oxygen.Editor.LivePreview` applies authoring state to the engine, through
   `Oxygen.Editor.Runtime`. View models do not call interop.
4. `Oxygen.Editor.Runtime` may call `Oxygen.Editor.Interop`; ordinary editor
   projects must depend on runtime services instead of interop.
5. `Oxygen.Editor.Interop` must be replaceable by stable managed service
   contracts. It exposes capabilities, not policy.
6. `Oxygen.Editor.Projects` owns project policy. UI projects ask it for
   project/cook decisions instead of duplicating path conventions.
7. `Oxygen.Managed.Assets` owns reusable asset/cook mechanics. It does not know which
   editor command or panel triggered them.
8. `Oxygen.Editor.ContentPipeline` owns editor workflow orchestration over
   import/cook/pak/inspect operations. It uses `Oxygen.Managed.Assets`,
   `Oxygen.Editor.Projects`, and native capabilities, but it does not own UI
   panels.
9. `Oxygen.Editor.Data` owns settings infrastructure. The owning feature still
   owns the meaning of each setting.
10. New project references must be justified by ownership. If the justification
    is "it was convenient", the dependency is wrong.
11. Cross-feature communication uses public service contracts, document
    contracts, or explicit messages owned by the sender/receiver boundary. Do
    not import another feature's view models to trigger behavior.

Existing broad references in the app and world editor are compatibility debt,
not precedent for new code.

## 7. Placement Decision Tree

Use this before adding a file:

1. **Is it reusable Oxygen Editor UI, but not reusable DroidNet UI?**
   Put it in `Oxygen.Editor.UI`. If it is useful outside Oxygen Editor, promote
   it to the appropriate DroidNet controls project instead.
2. **Is it material authoring?**
   Put material editor documents and UI in `Oxygen.Editor.MaterialEditor`.
   Put reusable material asset/import/cook primitives in `Oxygen.Managed.Assets`.
3. **Is it physics authoring data used by both physics scenes and scene
   objects?**
   Put shared physics domain objects in `Oxygen.Editor.Physics`. Put the
   physics scene sidecar editor UI and documents in
   `Oxygen.Editor.PhysicsEditor`.
4. **Is it pure scene data, component data, scene serialization, or scene-owned
   settings?**
   Put it in `Oxygen.Editor.World`.
5. **Is it UI or view-model behavior for editing an open scene?**
   Put it in the scene feature that shows it: `Oxygen.Editor.SceneEditor`
   (viewports and tools), `Oxygen.Editor.SceneExplorer` (the hierarchy) or
   `Oxygen.Editor.SceneInspector` (properties).
6. **Is it a scene mutation, undo/redo operation, dirty-state transition, or
   validation trigger?**
   Put it in `Oxygen.Editor.Scenes`, with the scene's edits and their rules.
7. **Is it a reusable asset identity, catalog, import, cook, or loose-index
   primitive?**
   Put it in `Oxygen.Managed.Assets`.
8. **Is it editor tooling orchestration for import, cook, pak, inspect, asset
   jobs, or pipeline diagnostics?**
   Put it in `Oxygen.Editor.ContentPipeline`.
9. **Is it project-specific content root, cook scope, manifest orchestration,
   or project settings policy?**
   Put it in `Oxygen.Editor.Projects`.
10. **Is it browsing, filtering, picking, or diagnosing assets for the user?**
    Put it in `Oxygen.Editor.ContentExplorer`.
11. **Is it embedded engine lifecycle, surface/view leasing, runtime settings
    application, cooked-root mounting, or runtime diagnostics?**
    Put it in `Oxygen.Editor.Runtime`.
12. **Is it a native Oxygen Engine operation?**
    Put the native bridge in `Oxygen.Editor.Interop`. Runtime/live-preview
    operations are exposed through `Oxygen.Editor.Runtime`; content/cooker/tool
    operations are exposed through `Oxygen.Editor.ContentPipeline`.
13. **Is it app startup, DI, top-level routing, window placement, or shell
    composition?**
    Put it in `Oxygen.Editor`.
14. **Is it the first experience before a project is open?**
    Put UI in `Oxygen.Editor.ProjectExplorer`; put project persistence/policy in
    `Oxygen.Editor.Projects`.
15. **Is it durable settings infrastructure?**
    Put infrastructure in `Oxygen.Editor.Data`; put setting ownership and UI in
    the feature that owns the setting's meaning.
16. **Is it only a helper shared by two features?**
    Do not create a generic dumping ground. Move the helper to the lowest
    existing owner that can own the concept, or define a small contract.

## 8. Settings Placement

Durable settings follow [settings-architecture.md](lld/settings-architecture.md):

| Setting Scope                                | Placement                                                    |
| -------------------------------------------- | ------------------------------------------------------------ |
| Editor setting infrastructure                | `Oxygen.Editor.Data`                                         |
| Editor setting meaning and app-wide defaults | `Oxygen.Editor` or the owning feature                        |
| Project settings                             | `Oxygen.Editor.Projects`                                     |
| Workspace layout/state                       | `Oxygen.Editor` shell services and editor data               |
| Scene environment/render intent              | `Oxygen.Editor.World` plus `Oxygen.Editor.SceneInspector` UI |
| Runtime effective settings                   | `Oxygen.Editor.Runtime`                                      |
| Diagnostic overrides                         | app launch/debug infrastructure                              |

Do not add a setting only as a command-line switch or environment variable.
Diagnostic overrides are temporary and must not become product configuration.

## 9. Authoritative Folder Map

New work uses this target organization:

```text
projects/Oxygen.Editor.UI/src/
+-- Controls/               # Oxygen-specific reusable controls/widgets
+-- Fields/                 # reusable editor field controls and VMs
+-- Overlays/               # validation, selection, badge, inline overlays
+-- Styles/                 # Oxygen editor styling resources
```

```text
projects/Oxygen.Editor.Composition/src/
+-- EditorModule.cs        # the editor scope's modules and the top-level routes
+-- *Module.cs             # the composition root's own modules: application, data, engine
+-- Workspace/             # the scene workspace window: its modules, routes and layout
```

```text
projects/Oxygen.Editor.ContentExplorer/src/
+-- Panes/ContentFolders/   # content folders and mounts tree, mount dialogs
+-- Panes/Assets/           # asset rows, details, actions and layouts
+-- State/                  # search, filters, presentation and selection
+-- Importing/              # scene and texture import dialogs
+-- Relocation/             # rename, move, copy and delete as the user asks
+-- Authoring/              # New Script, Input File, Scene and Material
```

```text
projects/Oxygen.Editor.ContentPipeline/src/
+-- Jobs/                   # import/cook/pak/inspect job model
+-- Import/                 # editor-facing import orchestration
+-- Cook/                   # editor-facing cook orchestration
+-- Pak/                    # packaging orchestration
+-- Inspect/                # cooked/source/descriptor inspection services
+-- Diagnostics/            # pipeline diagnostics model
+-- Native/                 # managed adapters over native engine tooling APIs
```

```text
projects/Oxygen.Editor.Runtime/src/
+-- Engine/                 # lifecycle, settings, surfaces, views, mounts
+-- Input/                  # editor input translation into runtime input
+-- Diagnostics/            # runtime-facing diagnostics models
```

```text
projects/Oxygen.Managed.Assets/src/
+-- Model/                  # asset records, references, typed assets
+-- Catalog/                # catalog/query implementations
+-- Import/                 # import model and import plugins
+-- Cook/                   # reusable cook writers/services
+-- Persistence/            # cooked index/document structures
+-- Validation/             # cooked output validation
```

Existing folders may differ during migration. New work follows this map instead
of deepening legacy folders.

## 10. Cross-Cutting Feature Pattern

Most editor features touch several projects. Split them by product:

| Feature Part                             | Placement                                  |
| ---------------------------------------- | ------------------------------------------ |
| Authoring data                           | `Oxygen.Editor.World`                      |
| Reusable Oxygen editor widget            | `Oxygen.Editor.UI`                         |
| User-facing editor                       | owning feature UI project                  |
| Command/dirty/undo integration           | owning feature UI project command services |
| Reusable asset/cook primitive            | `Oxygen.Managed.Assets`                    |
| Import/cook/pak/inspect tooling workflow | `Oxygen.Editor.ContentPipeline`            |
| Project policy or cook orchestration     | `Oxygen.Editor.Projects`                   |
| Live runtime application                 | `Oxygen.Editor.Runtime`                    |
| Native engine call                       | `Oxygen.Editor.Interop`                    |
| Validation result model/presentation     | owner service plus UI presentation         |

Example: a new scene component should not be implemented as one large
patch to one project. Its domain type goes in `World`; its inspector goes in
`SceneInspector`; its asset references use `Assets`; its cook policy goes through
`Projects`; its live operation goes through `Runtime`.

## 11. Feature Module Examples

These examples are normative:

| Scenario                                              | Placement                                                                                                                                                                                                                           |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Widget reused by multiple Oxygen editor features      | `Oxygen.Editor.UI`, unless it is generic enough for DroidNet controls.                                                                                                                                                              |
| Asset pipeline operations: import, cook, pak, inspect | `Oxygen.Editor.ContentPipeline` for tooling services; `Oxygen.Managed.Assets` for reusable primitives; `ContentExplorer` or owning editor for panels; `Projects` for project policy; `Interop` only for native engine capabilities. |
| Material editor                                       | `Oxygen.Editor.MaterialEditor` for material documents/UI/tools; reusable material source/asset/cook primitives in `Oxygen.Managed.Assets`; preview/runtime work through `Runtime` and `Interop`.                                    |
| Physics scene sidecar editor                          | `Oxygen.Editor.PhysicsEditor` for physics scene UI/documents/tools; shared physics authoring model in `Oxygen.Editor.Physics`; sidecar generation through `ContentPipeline`; project policy through `Projects`.                     |
| Scene component editor                                | Domain component in `World`; inspector in `SceneInspector`; reusable fields in `Oxygen.Editor.UI`; live operation through `Runtime`; native operation through `Interop`.                                                            |

## 12. New Project Rule

Create a new editor project only when all are true:

1. It owns a durable product or subsystem, not just a folder preference.
2. Its dependency set would otherwise pollute an existing lower layer.
3. It can expose a small public contract.
4. It can be tested independently.
5. It can follow the canonical `README.md` / `src` / `tests` / `docs` /
   `samples` project shape and the repository MSBuild contract.

If any condition is false, use a subfolder under the existing owner.

`Oxygen.Editor.MaterialEditor` and `Oxygen.Editor.PhysicsEditor` are planned
full editor modules. They own their document/workspace, editing tools,
validation, and lifecycle. Each editor, explorer, inspector and passive panel
with its own module and tests is a project of its own
([editor-module-architecture.md](lld/editor-module-architecture.md)), such as
`Oxygen.Editor.Output`; it contributes its parts through the Shell's
`IFeatureModule`.

## 13. Test Placement

Tests live with the owner they validate:

- domain and serialization tests in the domain project tests
- UI/view-model tests in the owning feature editor tests
- asset/cook primitive tests in `Oxygen.Managed.Assets` tests
- project policy tests in `Oxygen.Editor.Projects` tests
- runtime service tests in `Oxygen.Editor.Runtime` tests
- end-to-end editor workflow tests at the highest feature/workflow owner

A lower-layer test must not reference a higher-layer project just to validate a
workflow. Move the workflow test upward.

### 13.1 Module tests

- Each module is `README.md`, `src` and `tests`, with `tests/Unit`,
  `tests/Integration` and `tests/Benchmarks` projects as needed (`Unit.UI`,
  `Integration.UI` and `Benchmarks.UI` where the tests need the WinUI host).
  Its fixtures are its own, in `tests/TestSupport`, or in the module's
  `testsupport` folder for fixtures and probes that several of its test
  projects share.
- A test lives with the module whose behavior it checks. Other modules fake its
  public ports or use the real ones, and never share its fixtures: no test
  project links another module's fixture files.
- Internals are visible only to the module's own tests
  ([editor-module-architecture.md](lld/editor-module-architecture.md) decision
  4).

### 13.2 Module test kits

A module that other modules test against publishes a test kit,
`projects/<Module>/testing/<Module>.Testing.csproj`, referenced only by test
projects, as `TestHelpers` is. A kit holds builders of the module's public
types and in-memory fakes of its ports; the module maintains it and checks its
fakes against the real service. Other tests use the kit: they do not hand-build
the module's types, re-mock its ports in each test or link its `testsupport`
files.

| Kit                                     | Holds                                                                                                                                                                                  |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.Projects.Testing`        | `TestProject` (a project and its mount folders on any `IFileSystem`), `TestSession`, `TestSessionHost`                                                                                 |
| `Oxygen.Editor.Scripting.Testing`       | `FakeScripts`, `FakeScriptAnalyzer`, `FakeScriptLibrary`, `AnalysisHold`                                                                                                               |
| `Oxygen.Editor.Input.Testing`           | `FakeInputSources`                                                                                                                                                                     |
| `Oxygen.Editor.Scenes.Testing`          | `TestSceneDocuments`, `TestScene`, `TestScenes`, `FakeNodeInteraction`                                                                                                                 |
| `Oxygen.Editor.Assets.Testing`          | `TestAssetEntries`                                                                                                                                                                     |
| `Oxygen.Editor.ContentPipeline.Testing` | Fakes of its ports (`FakeAssetRedirects`, `FakeContentCookCoordinator`, `FakeCookPublicationReader`, `FakeSurfaceProvider`), `NativeContentPipelineFixture`, `EngineContentPakFixture` |
| `Oxygen.Editor.Shell.Testing`           | `TestDocumentArea`                                                                                                                                                                     |
| `Oxygen.Editor.Interop.Testing`         | `NativeDllSearchPath`                                                                                                                                                                  |
| `Oxygen.Managed.Assets.Testing`         | `LooseCookedIndexFixture`, `PakFixture`                                                                                                                                                |
| `Oxygen.Managed.Core.Testing`           | `TemporaryNativeArtifacts`                                                                                                                                                             |

### 13.3 Test rules

- **Moq.** A test mocks a port with Moq only when its owner's kit has no fake
  for it and the test needs a few canned answers, a thrown failure or a check
  that a call was made. A port whose kit has a fake is used through the fake. A
  port that tests mock again and again with the same setups, or that needs
  state across calls (a store, events, holds), gets a fake in its owner's kit.
  Moq mocks ports only, never models or the module under test.
- **File system.** Unit tests of logic over files run on Testably's
  `MockFileSystem`; the real disk is for integration tests (atomic replace,
  watcher timing, locking, native tools). Code takes `IFileSystem`, and a
  project's files are read through `ProjectContext.FileSystem`.
- **Models** take required identity members and optional `init` members, not
  long positional constructors.
- **Codes, not text.** Every refusal carries a contract code (such as
  `TextureImportCodes`, through `TextureImportException.Code`), and tests assert
  the code, not the message text.
- **Containers.** A test that composes editor modules over a hand-built host
  container uses the host-equivalent `HostContainer` of `TestHelpers`.

## 14. Anti-Placement Rules

- Do not put authoring defaults in interop.
- Do not put project root/cook policy in the C++/CLI layer.
- Do not put cooked binary structure knowledge in UI view models.
- Do not let inspector view models directly call native interop.
- Do not add new editor state only as environment variables or command-line
  switches.
- Do not add durable settings without assigning scope, owner, storage,
  validation, and mutation path.
- Do not create `Common`, `Shared`, or `Utils` as a substitute for ownership.
- Do not put a feature in the app project because it is needed at startup.
  Startup composes features; it does not own them.
