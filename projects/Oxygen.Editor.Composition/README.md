# Oxygen.Editor.Composition

The editor's composition root and the code that needs it. It does three jobs:

- **Composition root**: the module lists of the editor scope, of a project
  session's scope and of every kind of window, the top-level routes and the
  window targets.
- **Application lifecycle**: the project session's host, project activation,
  the composition root's parts of closing a window (placement, the return to
  the Start window, the editor's exit), diagnostics and the engine's shutdown.
- **Top-level windows**: the pages of the workspace window and of the document
  windows, which open their scopes through the session.

The lifecycle code stays here because it needs the composition root's knowledge
of every module and scope. The application host, `Oxygen.Editor`, is the only
project that references it; it is a library so that tests can compose the same
registrations.

Design: [di-container-topology.md](../../design/editor/review/di-container-topology.md)
(sections 6.1 to 6.7 and 6.10) and
[editor-module-architecture.md](../../design/editor/lld/editor-module-architecture.md).

## Layout

| Path                     | Holds                                                                                             |
| ------------------------ | ------------------------------------------------------------------------------------------------- |
| `EditorModule.cs`        | The editor scope's modules, in order, the host's settings and windowing, and the top-level routes |
| `EditorWindowModules.cs` | The modules of every kind of window: the one place that shows what a window's scope contains      |
| `ApplicationModule.cs`   | Paths, storage, activation, window placement, the windows and the scope factory                   |
| `EditorDataModule.cs`    | The state database, editor settings and project and template usage                                |
| `EditorWindows.cs`       | The top-level windows' targets and pages, and a navigation that reports its failure               |
| `EngineModule.cs`        | The embedded engine and its shutdown with the host                                                |
| `Sessions/`              | The project session's host, its windows, `ProjectSessionModule` and its last window's close       |
| `Workspace/`             | The workspace window: its view model and view, and `WorkspaceModule`                              |
| `DocumentWindows/`       | The document window a tab is torn out into: its page and the factory that opens it                |
| `Services/`              | Activation, project activation, window placement and the editor's exit                            |
| `Diagnostics/`           | The operation-result store, its output-log adapter and `DiagnosticsModule`                        |

## Scopes

```mermaid
flowchart TB
    Editor["Editor scope<br/>host container"]
    Session["Project session scope<br/>while a project is open"]
    Workspace["Workspace window scope"]
    Document["Document window scope"]
    Explorer["Content Explorer scope"]
    Editor --> Session
    Session --> Workspace
    Session --> Document
    Workspace --> Explorer
```

Arrows are container parentage: a scope registers only what it owns and
resolves the rest from its parent ([DroidNet.Composition](../Composition/README.md)).

- `Program` builds the host, then calls `EditorModule.Register`. It extends the
  host's container with its settings and windowing, then
  `ScopeFactory.CreateRoot` registers the editor scope's modules
  (`IApplicationLayer`) and their activation lists: those of the Shell,
  Projects, Project Explorer, ContentPipeline, LivePreview, Materials, Textures
  and the Scene Editor's settings, then this project's own.
- `ProjectSessionHost` (`IProjectSessionHost`, declared by Projects) opens the
  session of a project under the editor scope, from
  `ProjectSessionModule.Compose`: the session modules of the Shell,
  ContentPipeline, Assets, Scripting, Input, LivePreview, Problems, Materials,
  Scenes and the Scene Editor's settings, then this project's own, which gives
  the session the open documents with the scene and material kinds, the
  preview content demand, and its own contexts over the state database. The
  session's tabs are the Shell's, in the session's scope. The session registers
  the loaded project and itself (`IProjectSession`) as instances, never a
  `ProjectContext`: a content mount change replaces the session's context
  through `ChangeContext`, and the services whose state comes from the mounts
  follow `ContextChanged`. Closing the session closes its windows, cancels its
  lifetime, then disposes its scope and what is left of its windows' scopes.
- `ISessionWindows` is the one way a window joins the session. `OpenWindow`
  creates the window's scope under the session's from the window's modules
  (`EditorWindowModules`), records in Aura's window manager that the window is
  the session's, and binds the window's tab strip to the session's tabs. Every
  window's scope ends when the window closes (the Shell's `WindowScopeClose`).
  `PrepareCloseDocumentsAsync` prepares closing the documents of the session's
  windows, leaving out the window whose own close prepares its documents.
- `ProjectActivationCoordinator` stages the project, recovers its publication
  and prepares closing the session's documents before any session changes. A
  cancelled close keeps the open session. Once committed, the previous session
  closes, the new one opens and the workspace window navigates into it; a
  navigation that reports a failure closes the new session again. Only the
  startup navigation to the Project Explorer stops the application.
- Closing a window goes through the Shell's `CloseAndExit`. The composition
  root's participants save the placement of the Start and workspace windows,
  return to the Start window when the session's last workspace window closes
  (the documents of the session's other windows close with it, the Start window
  opens before that window closes, then the session closes), and exit the
  editor when its last window closes otherwise: any session closes, then the
  engine shuts down.
- `WorkspaceViewModel` and `DocumentWindowViewModel` open their windows through
  `ISessionWindows` and resolve only through their window's scope. A tab
  dropped outside every strip opens a document window through
  `DocumentWindowFactory`, at the drop point.
- The Content Explorer creates its own scope under the workspace window's.

Each subsystem registers what it owns through its module, beside its code, one
module per layer it has services in. The Shell's window modules build a window's
contributions from the features they are given; `ProjectSessionModule` keeps
what composes several subsystems in the session: the open documents and the
preview content demand.

## Tests

`tests/Oxygen.Editor.Composition.UI.Tests.csproj` composes the editor scope over
a host-equivalent container (`HostContainer` in `TestHelpers`) and creates the
session, workspace window, document window and Content Explorer scopes through
the real factory.

- **Topology**: no undeclared shadow, activated services provided, shared
  identity whichever scope resolves first, no service locator outside the
  container infrastructure, scope handles only for scope owners, no collection
  of a parent's services, resolvable constructors, singleton views only where
  reviewed, and the reviewed delegate registrations; a session service takes no
  window-bound dependency.
- **Project session**: one session at a time, holding its project; a window it
  opens gets its id and is recorded as the session's; closing it cancels its
  lifetime and disposes its windows and services.
- **Project activation**: a cancelled close of the documents keeps the open
  session, a failed workspace navigation closes the session it opened, and a
  successful one replaces the open session.
- **Closing**: the session's last window opens the Start window, then closes
  the session, and stays open when the Start window does not open or another
  window of the session keeps its documents; the last window outside a session
  exits the editor.
- **Project switch**: the next session gets its own global history and its
  window its own project preferences; the gizmo preferences are the editor's.

The scope checks read constructors by reflection. A delegate registration is
opaque to them, so each one is listed, reviewed, in the test.

## Build and test

See [build workflows](../../tooling/doc/build.md).
