# Editor shell

Status: **early draft, under discussion** (no code follows it yet)

Last Updated: 2026-10-20 01:32

Scope: `Oxygen.Editor.Shell`, the application frame of the Oxygen Editor. It
defines the workbench model (sessions, windows, parts), what feature modules
contribute, the services parts consume, the notification system, and the changes
the shell needs in Aura, Docking and Routing. Module boundaries and naming are
in [editor-module-architecture.md](../lld/editor-module-architecture.md); scopes
and container mechanics are in
[di-container-topology.md](di-container-topology.md).

## 1. Purpose and rule

The editor has no workbench layer today. Features reach into the libraries:
`DocumentHostViewModel` takes `IWindowManagerService` and a `WindowId`, the
Scene Explorer takes a `WindowId` and a router, the Content Browser picks the
active window as a picker owner, `WorkspaceViewModel` finds panels with
`Dockable.FromId("cook")` and sets their titles, shortcuts route through a
`SceneShortcutMessage` answered by the first taker, Ctrl+Z is bound in four
views, and nothing builds a main menu.

The shell owns those concerns once. **Rule: features never reference Aura
windowing, Docking, window routing or a `WindowId`.** They contribute
descriptors and consume services scoped to the part they run as. The shell alone
turns that into Aura windows, Docking layouts and document lifecycles.

## 2. Workbench model

### 2.1 Scopes

Editor (process) > Project Session > Window, with session-owned panels, as in
[di-container-topology.md](di-container-topology.md) section 6.1. The shell
builds every scope through `DroidNet.Composition`.

### 2.2 Window kinds

A window kind is a descriptor. It maps to an Aura window category, a decoration
and a menu provider, and it declares its areas. The shell creates every window
through one factory; no keyed window registration (`wnd-we`, `wnd-pb`) remains.

| Kind            | Scope              | Areas                                                | Contributed by                                              |
| --------------- | ------------------ | ---------------------------------------------------- | ----------------------------------------------------------- |
| Start           | Editor, no session | One routed page                                      | ProjectExplorer                                             |
| Workspace       | Session            | Dock layout and a document area; a preset fills them | Shell (kind), composition root (the scene workspace preset) |
| Document window | Session            | One document area: a torn-out tab, a material window | Shell                                                       |
| Panel window    | Session            | One floating panel                                   | Shell                                                       |

### 2.3 Parts

- **Panel**: semantically unique by default, owned by the session, shown in at
  most one window at a time, moved between windows without being recreated.
- **Document**: one editor per `DocumentId`, living in one window's document
  area, moved by the detach and attach that `IDocumentService` already offers.

### 2.4 Layout and routing

Not decided. Routing stays unless an alternative is shown to be better on the
criteria below; this section records the comparison to make.

What routing gives the editor today:

- **URL as serialized state.** A window's content and a workspace's dock layout
  are one string, so persisting and restoring them needs no other format. The
  Content Browser restores its panes and selection this way.
- **History.** A sequence of URLs is a natural back and forward history; the
  Content Browser keeps one for its navigation.
- **Uniform composition.** View models are created by the container through
  routes, placed in outlets, and receive `IRoutingAware` navigation calls.
- **Addressability.** A URL can name a place (a panel, a folder, a document
  view) for deep links, restore and tests.

What the current use of routing for the dock layout costs:

- `DockingWorkspaceLayout` syncs layout changes back into the URL by scanning
  the process-global `Dockable.All`, so it sees other windows' dockables.
- Adding a dock route at runtime goes through the router's `MatchViewModelType`,
  which hard-codes `Children[0].Children[0]`.
- A route deleted by a change-set navigation leaves its view model undisposed.
- The router creates and disposes the view models of its routes, while unique
  panels must be owned by the session and survive any window.

The first three are defects in the current implementation, not limits of
routing. The fourth is the real design question. Two options:

| Option                     | Layout is                                                                                          | Keeps                                                         | Needs                                                                                   |
| -------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| A: routed layout (current) | The window's router URL; panel routes resolve the session's retained panel instead of creating one | URL persistence, history, uniform composition, addressability | Per-window router state, `IRetainedViewModel` in the router, the three defect fixes     |
| B: shell layout model      | A shell-owned tree of areas and docks, serialized per project and window kind                      | Direct ownership of panels by the registry                    | Its own serializer, its own history if wanted, a second composition path beside routing |

Option B is chosen only if it is shown better on persistence, history,
addressability, multi-window behavior and panel retention than option A with its
fixes. Panel-internal navigation (the Content Browser's left and right outlets
and its history) keeps routing in either option. Window placement is keyed by
window kind and instance, not by URL, in either option.

## 3. Contributions

Feature modules register contributions at startup. The registry validates them
once; a duplicate id or a reference to an unknown area, command or part fails
startup.

| Contribution               | Content                                                                                                  | Replaces today                                                          |
| -------------------------- | -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| `PanelDescriptor`          | Id, title, icon, multiplicity, default area, view model and view types                                   | `WorkspaceViewModel.RoutesConfig`, dock ids used with `Dockable.FromId` |
| `DocumentEditorDescriptor` | Metadata type, editor factory, capabilities (save, history)                                              | `DocumentHostViewModel`'s switch over the metadata types                |
| `CommandDescriptor`        | Id, label, icon, default key gesture, category                                                           | View-local relay commands and accelerators                              |
| `MenuContribution`         | Location (`main/edit`, `context/scene-node`), group, order, command id                                   | Local `MenuBuilder` code in three view models                           |
| `WindowKind`               | Frame, areas, Aura category, decoration, menu provider                                                   | Keyed `Window` registrations                                            |
| `WorkspacePreset`          | Initial panels and their positions for a workspace window (an initial URL under option A of section 2.4) | The URL hard-coded in `WorkspaceViewModel`                              |

## 4. Services offered to parts

| Service             | Shape                                                                                                                                                                                  | Scope   | Replaces                                                                                                                               |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Placement           | `OpenDocumentAsync(IDocumentMetadata, WindowPlacement)`, `ShowPanelAsync(PanelId, WindowPlacement)`, `OpenWindowAsync(WindowKind, preset)`                                             | Session | `Open*RequestMessage`, `ShowAssetRequestMessage`, `ShowCookingJobsRequestMessage`, `IPlayWorkspace.Reveal`, `ICookingWorkspaceActions` |
| Part host           | `IPartHost`: `Window` (observable, null while hidden), `IsShown`, `Title`, `Badge`, `Reveal()`, `Close()`, `Dialogs`, `Pickers`                                                        | Part    | `WindowId` injection, setting a dockable's title, the active window as dialog owner                                                    |
| Document host       | `IDocumentHost : IPartHost`: `DocumentId`, `UpdateMetadata` (title, dirty), `History`                                                                                                  | Part    | `UndoRedo.GetHistory(id)` in four view models, `IsActiveDocument()` checks                                                             |
| Context             | Active window, active part, active document per window; observable                                                                                                                     | Editor  | Ad hoc `FocusManager.GetFocusedElement` queries                                                                                        |
| Commands            | Handlers registered by parts; resolution along focused part, window's active document, window, session, editor; key bindings installed per window frame; can-execute and checked state | Session | `SceneShortcutMessage`, `SceneEditRequestMessage`, the workspace key map, duplicate Ctrl+Z/Y bindings                                  |
| Menus               | Main menu per window kind through Aura's `IMenuProvider`; context menus from the same contributions                                                                                    | Session | Local menu builders; there is no main menu                                                                                             |
| Close and exit      | Participants at document, window, session and application level; phases prepare (veto, prompt), commit, finalize; the shell is the only subscriber to Aura's `WindowClosing`           | Editor  | `WorkspaceCloseCoordinator`, window counting in `EngineShutdownService`, the router's `ReplaceTarget` close                            |
| Notifications       | Section 5                                                                                                                                                                              | Editor  | Local InfoBars as the only feedback                                                                                                    |
| Activity            | Long-running work with progress, shown by the frame and by notifications                                                                                                               | Editor  | `CookRunContext` progress visible only in the Cooking panel                                                                            |
| Clipboard and drag  | Typed payloads, session scoped, bridged to the system clipboard                                                                                                                        | Session | The Scene Explorer's private clipboard, per-feature drag formats                                                                       |
| Desktop integration | Reveal in File Explorer, copy text, recycle bin, launch the external code editor                                                                                                       | Editor  | `IAssetShell` and `CodeEditorService` inside the Content Browser                                                                       |

Example: the Inspector is focused and the user presses Ctrl+Z. The window
frame's key binding raises Undo; the Inspector part has no Undo handler, so
resolution reaches the window's active document; the scene editor's handler
undoes `IDocumentHost.History`. Undo then works the same from every panel in
every window, and the Edit menu shows the same enabled state.

## 5. Notifications

### 5.1 Goal

Any UI component can raise a notification, and every service reaches the user
through it indirectly (section 5.3). The user sees a notification as a toast in
an editor window, finds it later in the Notification Center, and receives it as
a Windows notification when no editor window is in the foreground.

### 5.2 Contract

The contract is part of the shell, beside its implementation and every
presentation. Feature modules reference the shell, so every part can post.
Domain modules do not reference the shell and do not post notifications.

```csharp
public interface INotificationService
{
    NotificationHandle Post(NotificationRequest request);
}

public sealed record NotificationRequest(
    NotificationSeverity Severity,          // Info, Success, Warning, Error
    string Title,
    string? Message = null)
{
    public string? Source { get; init; }                 // module or part id
    public string? GroupKey { get; init; }               // coalesces repeats
    public NotificationScope Scope { get; init; }        // Editor, Session, Window
    public IReadOnlyList<NotificationAction> Actions { get; init; } = [];
    public NotificationUrgency Urgency { get; init; }    // Normal, High
    public Guid? OperationId { get; init; }              // links an operation result
}

public sealed record NotificationAction(string Label, string CommandId, string? Argument = null);

public sealed class NotificationHandle
{
    public Guid Id { get; }
    public void Update(NotificationRequest request);     // progress, final state
    public void Dismiss();
}
```

Actions name commands, not delegates. A command id with a string argument
survives a Windows notification activation, which can arrive after the toast is
gone; the command service resolves it in the current context.

`Post` is callable from any thread; the shell marshals presentation to the right
dispatcher. A `Window` scope targets the window of the posting part (from its
`IPartHost`); the others target the active editor window.

### 5.3 Services and attention

Deciding to interrupt the user is presentation policy, so it stays out of the
domain. A domain service reports what happened through the contracts it already
has, and the UI decides whether that deserves attention:

- **Operation results.** A shell policy turns selected results into
  notifications (failures, partial successes, and results that carry a primary
  action), linked by `OperationId`. Existing publishers get toasts without
  change.
- **Domain events.** An event that needs the user's response (a file changed on
  disk, a reload is possible) is raised by the service as its own event. The
  feature that owns the related UI subscribes and posts the notification with
  the right actions. The service never learns about toasts.

### 5.4 Presentation

| Surface              | Behavior                                                                                                                                                                                                                                                                         |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Toast                | Shown in the target window's frame, stacked in a corner, at most three visible. Info and Success dismiss after a few seconds, Warning after longer, Error stays until dismissed. A repeat with the same `GroupKey` updates the existing toast. Announced to assistive technology |
| Notification Center  | A passive panel (`Oxygen.Editor.NotificationCenter`) listing the history, filterable by severity and source, with an unread count shown by the frame. Editor-scoped history, bounded; session notifications are kept but their actions are disabled once the session ends        |
| Windows notification | When no editor window is in the foreground, notifications of High urgency or Warning and above go to Windows through the Windows App SDK app notifications. Activating one restores the editor window and runs the action                                                        |

User settings control the Windows notification threshold and a do-not-disturb
mode (for example while playing a scene).

### 5.5 Relation to operation results, Output and Problems

- **Operation results** stay the diagnostic record of an operation. Every result
  still goes to Output.
- **Notifications** are requests for attention, raised by UI parts and by the
  shell's result policy (section 5.3).
- **Output** remains the log; **Problems** remains the list of persistent
  problems; the **Notification Center** is the history of attention requests. A
  "Details" action opens Output filtered to the linked operation.

### 5.6 To verify

- The editor runs unpackaged. App notifications for unpackaged Windows App SDK
  applications need registration at startup and activation handling in
  `Program`; the exact requirements of the version in use must be confirmed.
- Aura has no overlay layer in the window frame; toasts need one (section 9).

## 6. Close and exit

One ordered negotiation replaces today's independent `WindowClosing`
subscribers:

1. Prepare: document participants (the existing `IDocumentCloseParticipant`),
   then window and session participants, can veto or prompt. One prompt lists
   every dirty document of the scope being closed.
2. Commit: documents close, panels detach, windows close.
3. Finalize: session disposal, then, on application exit, editor-level
   finalizers such as engine shutdown.

Closing the last workspace window of a session returns to the Start window
instead of exiting; exiting is an explicit command or closing the Start window.
Whether the Windows App SDK keeps the process alive with no window during the
swap is the open check of issue #69.

## 7. Shell project layout

```text
Oxygen.Editor.Shell/src/
+-- Contributions/   # descriptors, registry, validation
+-- Sessions/        # session host, session events
+-- Windows/         # window kinds, frame, factory, registry, placement store
+-- Layout/          # layout store and Docking renderer (section 2.4)
+-- Panels/          # panel registry, part host
+-- Documents/       # document host, document manager, close participants
+-- Commands/        # command service, key bindings, menus
+-- Notifications/   # service, policy, toasts, Windows delivery
+-- Activity/        # long-running work
+-- Desktop/         # file explorer, clipboard, code editor launch
```

## 8. Not in scope of issue #69

Issue #69 builds the services that replace existing mechanisms: placement, part
and document hosts, context, command routing for today's commands, close and
exit, layout, and the library fixes. These are new user-facing features to plan
separately, though the services above are shaped for them:

- Notification service, toasts, Notification Center and Windows delivery
  (section 5), planned for the near future.
- Main menu with File, Edit and Window menus.
- Status bar.
- Command palette and user key bindings.

## 9. Library changes

| Library | Change                                                                                                                                                                                                                                                      | Why                                                                            |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Aura    | Per-window dialog gate instead of the static `DialogGate`                                                                                                                                                                                                   | A dialog in one window blocks every other window today                         |
| Aura    | `RoutedWindow.LoadContent` skips retained view models                                                                                                                                                                                                       | A floating panel window would dispose the panel                                |
| Aura    | An overlay layer in the window frame                                                                                                                                                                                                                        | Toasts                                                                         |
| Docking | Dockable and dock registries, id counter and docking-mode messenger per `IDocker`; layout export and import under option B of section 2.4                                                                                                                   | Two workspace windows collide on ids; layout persistence                       |
| Docking | Floating mode is a stub; a floating panel becomes a shell panel window                                                                                                                                                                                      | No floating implementation exists                                              |
| Routing | Window targets created by a factory; `DisposeRouteTree` and change-set deletes respect retained view models; per-window router state instead of `Dockable.All`; runtime route addition without the hard-coded lookup; `NavigationError` carries the failure | Dynamic windows; panel survival; option A of section 2.4; diagnosable failures |

## 10. Open questions

1. Routed layout or a shell layout model (section 2.4). Routing stays unless the
   alternative is shown better.
2. Panel multiplicity `Many` (two independent explorers, a pinned inspector):
   supported by descriptors, but no panel needs it yet.
3. Notification history persistence across editor restarts.
4. Where user notification settings live (Editor settings through
   `IEditorSettingsManager`, application scope).
