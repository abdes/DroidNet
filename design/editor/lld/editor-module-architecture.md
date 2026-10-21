# Editor Module Architecture LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define the project structure of the Oxygen Editor after the decomposition of
`Oxygen.Editor.WorldEditor` (issue #69): which modules exist, what each owns,
how they are named, which references are allowed, and where logic may live. The
application frame (windows, panels, documents, commands, notifications) is in
[editor-shell.md](../review/editor-shell.md), and documents, histories, saving
and cooking in [editor documents](editor-documents.md). The scope chain and
container mechanics are in
[di-container-topology.md](../review/di-container-topology.md) sections 2 to 6;
this design replaces its section 7.

## 2. PRD Traceability

| ID         | Coverage                                                                                               |
| ---------- | ------------------------------------------------------------------------------------------------------ |
| `GOAL-001` | Each product area has one module that owns it, so a feature is found, changed and tested in one place. |
| `REQ-006`  | Authoring changes go through the domain modules' edits, recorded into the histories the Shell routes.  |
| `REQ-022`  | Operation results come from the domain modules and are presented by the Shell and passive panels.      |

## 3. Architecture Links

- [editor-documents.md](editor-documents.md): documents, histories, saving and
  cooking.
- [asset-family-modules.md](asset-family-modules.md): the family modules of the
  domain layer.
- [editor-shell.md](../review/editor-shell.md),
  [assets-module.md](assets-module.md), [scenes-module.md](scenes-module.md),
  [live-preview.md](live-preview.md): the designs of the Shell and of the
  Assets, Scenes and LivePreview modules.
- [di-container-topology.md](../review/di-container-topology.md): the scope
  chain and container mechanics.

## 4. Current Baseline

The moves that bring the code to this design, and what each has done, are the
slices of the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Decisions

1. `Oxygen.Editor.WorldEditor` is dissolved. It is not a host and not an editor:
   its generic workspace machinery moves to the Shell and its scene editing
   parts move to the scene modules. "The editor" is the Shell plus the modules
   it hosts; the scene workspace is one window kind among several.
2. The Shell is the central application frame. Features plug into it through
   contributions and consume its services; they never reference Aura windowing,
   Docking, window routing or a `WindowId` directly.
3. UI projects contain views, view models and presentation only. Services,
   workflows and domain logic live in non-UI modules. Content pipeline logic
   (import, cook, publication, relocation, inspection) lives only in
   `Oxygen.Editor.ContentPipeline`.
4. A project's internals are visible only to its own test project. No
   `InternalsVisibleTo` grant to any other assembly, test or production. A test
   that needs another module's private state uses that module's public API or
   moves to that module's tests.
5. Each module owns its outbound ports. A module that needs something from a
   lower-level capability it must not reference (the engine, for example)
   declares the interface; the module that may reference the capability
   implements it.
6. Classes, interfaces and APIs may be renamed when a name misstates what it
   owns. A move commit keeps names so the compiler checks the move; a rename
   commit follows it.

### 5.2 Layers

```text
Composition root        Oxygen.Editor
Shell                   Oxygen.Editor.Shell
Feature UI              editors, explorers, inspectors, passive panels
Shared feature UI       Oxygen.Editor.Pickers
Domain services         Scenes, Assets, LivePreview, ContentPipeline, Projects
                        families: Scripting, Input, Textures, Materials
UI kit                  Oxygen.Editor.Controls
Foundation              World, Documents, Schemas, Data, Runtime, Interop,
                        Managed.Core, Managed.Assets, Storage, Composition,
                        Aura, Docking, Routing, Oxygen.Editor.Routing
```

References point down only. A feature references the Shell, the domain modules
it uses, the shared feature UI and the UI kit. The shared feature UI is what
several features show alike (the asset picker); it references domain modules,
the UI kit and foundation, never the Shell or a feature. A domain module references other domain modules below it
and foundation, never the Shell or a feature. The Shell references foundation
only. The composition root references everything and is the only project that
does.

`Oxygen.Editor.Routing` is foundation: it is the local routing over DroidNet
Routing that the Shell's layout and the panels' own navigation both use, and it
knows no editor module.

### 5.3 Naming conventions

Proposed conventions; the direction is agreed, individual names are open.

| Kind      | Purpose                                                                         | Name                | Examples                                              |
| --------- | ------------------------------------------------------------------------------- | ------------------- | ----------------------------------------------------- |
| Editor    | Creates and modifies a document                                                 | `XxxEditor`         | `SceneEditor`, `MaterialEditor`, `PhysicsEditor`      |
| Explorer  | Explores and discovers hierarchical or complex content; never "Browser"         | `XxxExplorer`       | `SceneExplorer`, `ContentExplorer`                    |
| Inspector | Shows, and optionally edits, the properties of one entity                       | `XxxInspector`      | `SceneInspector`, `CookedInspector`                   |
| Passive   | Panels and components that present information without owning an entity's state | Neutral noun        | `Output`, `Problems`, `Cooking`, `NotificationCenter` |
| Domain    | Non-UI services for one product area                                            | Plural noun or area | `Scenes`, `Assets`, `LivePreview`, `ContentPipeline`  |

Type names follow the module: a panel's view model is `<Module>ViewModel`, its
contracts carry the module's noun, and no public type carries the name of a
panel it does not belong to.

### 5.4 Dependency graph

```mermaid
flowchart TB
    App["Oxygen.Editor - composition root"]
    subgraph Features["Feature UI - editors work on their family's model"]
        PE["ProjectExplorer"]
        SEx["SceneExplorer"]
        SIn["SceneInspector"]
        SEd["SceneEditor"]
        CEx["ContentExplorer"]
        MEd["MaterialEditor"]
        TEd["TextureEditor"]
        CIn["CookedInspector"]
        Prb["Problems"]
        Ck["Cooking"]
        Out["Output"]
        His["History"]
    end
    Shell["Shell - application frame<br/>IDocumentService, histories, Undo and Redo"]
    subgraph Domain["Domain modules - shared, headless, engine or native work"]
        Scenes["Scenes<br/>scene authoring and edits"]
        Assets["Assets<br/>catalog, feed, rules"]
        CP["ContentPipeline<br/>import, cook, relocation"]
        Fam["Scripting, Input, Textures"]
        Mat["Materials"]
        Live["LivePreview<br/>engine sync, Play, native adapters"]
        Proj["Projects<br/>asset to file"]
    end
    subgraph Found["Foundation"]
        World["World<br/>scene model"]
        Docs["Editor.Documents"]
        MA["Managed.Assets"]
        Sto["Storage"]
        RT["Runtime"]
        Int["Interop"]
        Lib["Aura, Docking, Routing, Oxygen.Editor.Routing,<br/>DroidNet.Documents, Composition"]
    end
    App --> Features
    App --> Live
    Features --> Shell
    SEx --> Scenes
    SIn --> Scenes
    SIn --> Assets
    SEd --> Scenes
    SEd --> Live
    CEx --> Assets
    CEx --> CP
    MEd --> Assets
    MEd --> Mat
    TEd --> Fam
    SIn --> Fam
    CEx --> Fam
    CIn --> CP
    Ck --> CP
    Prb --> Fam
    Prb --> Live
    PE --> Proj
    Live -- previews open documents --> Scenes
    Live -- implements port --> Assets
    Live -- implements port --> Fam
    CP -- implements IScriptLibrary --> Fam
    Live --> RT
    Live --> Int
    Scenes --> CP
    Scenes --> Proj
    Assets --> CP
    CP --> Proj
    CP --> Fam
    Scenes --> Fam
    Fam --> Proj
    Mat --> CP
    Domain --> World
    CP --> MA
    CP --> Docs
    Proj --> Sto
    Shell --> Docs
    Shell --> Lib
```

Edges from features to `Managed.Core`, `World` and `Controls` are omitted for
legibility. LivePreview is the only domain module that references Runtime and
Interop; Scenes knows no engine (LivePreview previews its open documents), and
Assets and the family modules reach the engine and native code only through the
ports LivePreview implements.

## 6. Ownership

| Module                             | Kind      | Owns                                                                                                                                                                                                                                                           | Must not own                                            |
| ---------------------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| `Oxygen.Editor`                    | Root      | `Program`, module composition, workspace presets, activation of the first window                                                                                                                                                                               | Feature policy, services                                |
| `Oxygen.Editor.Shell`              | Shell     | Session host, window kinds and frames, docking host, document host and manager, panel registry, placement, workbench context, command routing, menus, close and exit, layout store, notifications (contract, result policy, presentation), desktop integration | Scene, asset or pipeline policy                         |
| `Oxygen.Editor.ProjectExplorer`    | Explorer  | Start experience: recent projects, templates, create and open                                                                                                                                                                                                  | Project persistence rules (Projects)                    |
| `Oxygen.Editor.SceneEditor`        | Editor    | Scene document editor: viewports, gizmos, transform tools, toolbar, play bar                                                                                                                                                                                   | Scene commands (Scenes), engine sync (LivePreview)      |
| `Oxygen.Editor.SceneExplorer`      | Explorer  | Hierarchy panel, drag and drop, adapter tree                                                                                                                                                                                                                   | Mutation and organizer model (Scenes)                   |
| `Oxygen.Editor.SceneInspector`     | Inspector | Node and component property editing                                                                                                                                                                                                                            | Commands and edit sessions' model (Scenes)              |
| `Oxygen.Editor.ContentExplorer`    | Explorer  | Asset navigation, filtering, views, import dialogs                                                                                                                                                                                                             | Relocation and creation logic (ContentPipeline, Assets) |
| `Oxygen.Editor.MaterialEditor`     | Editor    | Material document: edits Materials' `Material`                                                                                                                                                                                                                 | Material file format (Managed.Assets), cook primitives  |
| `Oxygen.Editor.TextureEditor`      | Editor    | Texture document: source image, cube layout, settings, preview                                                                                                                                                                                                 | Import authoring (Textures), cook (ContentPipeline)     |
| `Oxygen.Editor.CookedInspector`    | Inspector | Cooked package inspection panel                                                                                                                                                                                                                                | Inspection logic (ContentPipeline)                      |
| `Oxygen.Editor.Output`             | Passive   | Log view over the output buffer                                                                                                                                                                                                                                | Log production                                          |
| `Oxygen.Editor.Problems`           | Passive   | Problem list over every `IAssetProblemSource`: the family modules' problems and the runtime problems of LivePreview                                                                                                                                            | Problem production                                      |
| `Oxygen.Editor.Cooking`            | Passive   | Cook job list over `ICookRunService`                                                                                                                                                                                                                           | Cook orchestration (ContentPipeline)                    |
| `Oxygen.Editor.NotificationCenter` | Passive   | Notification history panel                                                                                                                                                                                                                                     | Notification delivery (Shell)                           |
| `Oxygen.Editor.History`            | Passive   | The active document's history and the global history; undo or redo to a chosen entry                                                                                                                                                                           | The histories (Shell)                                   |
| `Oxygen.Editor.Scenes`             | Domain    | The scene document kind, scene authoring (`SceneAuthoring`) and its edits, selection, clipboard model, node lock and visibility                                                                                                                                | WinUI views, engine calls, histories (Shell)            |
| `Oxygen.Editor.Assets`             | Domain    | Asset catalog and feed, identity rules, the rules of properties that reference assets; owns the `IAssetRuntimeStatusSource` port                                                                                                                               | Browser state, engine calls                             |
| `Oxygen.Editor.LivePreview`        | Domain    | Engine sync, content demand, mount changes, play session, runtime session start, the preview of open scene documents (`IScenePreview`), the engine adapters for the ports of Assets and Scripting (`IScriptAnalyzer`), runtime problems                        | Authoring state                                         |
| `Oxygen.Editor.ContentPipeline`    | Domain    | Import, cook, publication, relocation, inspection, the asset creation workflow over the families' templates; implements Scripting's `IScriptLibrary`                                                                                                           | Panels, prompts, family rules                           |
| `Oxygen.Editor.Scripting`          | Family    | `Script`, `ScriptParameter`, script slot rules, script facts, the `IScriptAnalyzer` and `IScriptLibrary` ports, script tooling, templates and codes                                                                                                            | Views, the native call (LivePreview)                    |
| `Oxygen.Editor.Input`              | Family    | `InputContainer`, `InputAction`, `InputMappingContext`, input rules and starting context priorities, input facts, templates                                                                                                                                    | Views, the cook                                         |
| `Oxygen.Editor.Textures`           | Family    | `Texture`, cube layouts, texture import authoring                                                                                                                                                                                                              | Views, the cook                                         |
| `Oxygen.Editor.Materials`          | Family    | `Material`, its document kind, edits, validation, property catalog, templates and codes                                                                                                                                                                        | Views                                                   |
| `Oxygen.Editor.Controls`           | UI kit    | Reusable editor controls                                                                                                                                                                                                                                       | Feature policy                                          |

`Oxygen.Editor.Projects`, `Runtime`, `World`, `Documents`, `Schemas`, `Data` and
the DroidNet libraries keep their ownership, with the library changes listed in
[editor-shell.md](../review/editor-shell.md) section 9.

## 7. Domain Modules

### 7.1 Assets

The editor's live view of the active project's assets: project assets, mounted
libraries and engine built-ins, one entry per logical asset, with cook and
preview state, the identity rules and the rules of properties that reference
assets. Its public surface, its ports and its tests are specified in
[assets-module.md](assets-module.md). Script facts are Scripting's `Script`
([asset family modules](asset-family-modules.md)).

### 7.2 Scenes

Scene authoring: the scene document kind, `SceneAuthoring` and its edits, edit
sessions, selection, the Explorer layout, node lock and visibility and the
session clipboard model. Its changes are recorded into the history each document
is given (documents design decision 1). Scenes knows no engine: LivePreview
previews the open scene documents by observing their changes (`IScenePreview`,
[live-preview.md](live-preview.md) decision 3).

### 7.3 LivePreview

Everything that applies authoring state to the embedded engine: scene engine
sync, content demand, mount changes, runtime session start, play session, and
the runtime problem source. It is the only domain module that references
`Oxygen.Editor.Runtime`.

### 7.4 ContentPipeline

Import, cook, publication, relocation, inspection and asset creation. It grants
internals to its own tests only; other modules' tests arrange state through its
public API.

### 7.5 Family modules and editors

Each authored asset family has one domain module that owns its model, rules,
problems, facts, templates and codes: Scripting, Input, Textures and Materials
([asset family modules](asset-family-modules.md)). World holds the scene model
only. An editor edits its family's model: the material editor and the future
texture and input editors; scripts are edited in the external code editor. The
cook calls the family rules and never states them.

## 8. Logic In UI Code

The UI keeps the interaction (prompts, selection, focus); each operation lives
with its owner.

| Operation                                                                  | Owner                                                                                 |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| Knowing which inputs are unsaved before a cook                             | Shell (`IUnsavedDocuments`); the cook reads saved files only (documents design 5.3.3) |
| Launching the external code editor with its preferences                    | Shell desktop integration                                                             |
| Copy text, reveal in File Explorer, recycle bin                            | Shell desktop integration                                                             |
| Panel reveal, save, open document                                          | Shell placement and commands                                                          |
| Selecting a node                                                           | Scenes selection                                                                      |
| Runtime session start for a project                                        | LivePreview                                                                           |
| Merging script and runtime problems                                        | Problems, as presentation over the domain contracts                                   |
| Validation and file reading behind cooked inspection and the cooking panel | ContentPipeline                                                                       |

## 9. UI Surfaces

Feature UI projects (editors, explorers, inspectors, passive panels) contain
views, view models and presentation only (decision 3). They plug into the Shell
through contributions and consume its services (decision 2). One reaches the
engine: the SceneEditor's viewports draw through Runtime's surfaces, views and
input, which need the `SwapChainPanel` ([live preview](live-preview.md) decision
5); it never references Interop.

## 10. Persistence And Round Trip

Each module persists only what it owns: documents through their kinds and the
Shell's document service ([editor documents](editor-documents.md)), settings
through their owners, cooked content through ContentPipeline.

## 11. Cook / Mount / Runtime Behavior

ContentPipeline owns import, cook, publication, relocation and inspection;
LivePreview owns everything that applies authoring state to the embedded engine.
No other module references the engine, except the SceneEditor, whose viewports
use Runtime's surfaces, views and input and never Interop (section 9).

## 12. Operation Results And Diagnostics

Domain modules produce operation results and problems; the Shell and the passive
panels (Output, Problems, Cooking) present them.

## 13. Dependency Rules

References point down the layers of 5.2 only; a module's internals are visible
to its own tests only (decision 4); each module owns its outbound ports
(decision 5).

## 14. Validation Gates

Each module has its own test project; a UI test project only where it has XAML.
A test lives with the module whose behavior it checks. Cross-module workflows
are tested at the highest owner (the composition root's integration suite). A
tooling unit test, `tooling/tests/test_editor_module_boundaries.py`, enforces
the layer rules of 5.2, the engine rule of 5.4 and the internals rule of
decision 4.

## 15. Open Issues

Individual names of section 5.3 are open; the direction is agreed.
