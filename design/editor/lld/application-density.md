# Application Density LLD

Status: `final V0.1 design — implementation not started`

Summary: one application-wide **density**, Comfortable or Compact, applies to
every shared control, every editor module and every window, and switches at
runtime without a restart. Density is a set of theme resources that controls
read by name. A switch merges or removes one overlay dictionary at Application
scope and refreshes the live trees. The new
[DroidNet.Density](#54-the-droidnetdensity-package) package owns that mechanism;
shared controls depend on it and on nothing from Oxygen or Aura, and need no
consumer setup. Editor modules take sizes from shared tokens, styles and
structures. Read the [platform behavior](#51-required-platform-behavior), the
[resource contract](#53-resource-contract-for-shared-controls), the
[decisions](#151-decisions) and the
[implementation slices](#16-implementation-slices).

## 1. Purpose

Goals:

1. Every shared control (`DroidNet.Controls.*`, Docking, Aura controls)
   supports both densities.
2. Compact uses WinUI's own compact resources and adds library resources where
   WinUI has none. Stock WinUI controls honor the mode with no extra code.
3. In the editor, density is application-wide and cascades to every window,
   pane, flyout, tooltip, popup and dialog. The one exception is window chrome
   (§5.8).
4. Shared controls depend on nothing from Oxygen or any application, so they
   ship and are reused as standalone NuGet packages.
5. Editor modules take sizes only from the shared density vocabulary. The
   structure makes bypassing unsupported and unnecessary; nothing scans source
   to enforce it (§5.9).
6. The mode changes at runtime from application settings and applies at once.

Non-goals: text scaling and DPI (the OS owns them); theme (Light/Dark);
width-responsive layout adaptation (§4 terms); per-window or per-module
density; more than two modes.

## 2. PRD Traceability

`REQ-046` (this design). `REQ-022` does not apply: switching density is an
appearance preference, not an authoring operation (§12).
[PROGRESS](../ui/PROGRESS.md) lists density qualification as remaining work,
and [PROPERTY_EDITING](../ui/PROPERTY_EDITING.md) records a Comfortable-density
selector as outside its focused evidence; this design supplies both.

## 3. Architecture Links

- [ARCHITECTURE.md](../ARCHITECTURE.md): module ownership and dependency
  direction.
- [Settings architecture §5](./settings-architecture.md#5-setting-placement):
  owns the Appearance (theme, density) setting row.
- [Oxygen rules](../../oxygen/RULES.md): documentation and engineering rules.

## 4. Current Baseline

Terms used below:

| Term              | Meaning                                                                                                                                                                                        |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Density           | Comfortable or Compact: the size and spacing of controls. Application-wide.                                                                                                                    |
| Layout adaptation | Width-driven or explicit layout switches: `NumberBox.IsCompact` (label placement), `ToolBar.IsCompact`, `MenuOptions.IsCompact` (collapsed menu bar). They are not density and stay unchanged. |
| Key               | A named theme resource holding a size, padding or font size. Read with `{ThemeResource}` only.                                                                                                 |
| Defaults          | A library's complete set of Comfortable key values, registered at the head of `Application.Resources` (§5.3).                                                                                  |
| Overlay           | A dictionary merged after the defaults that overrides the keys Compact changes. Comfortable is the absence of every overlay: stock WinUI plus the library defaults.                            |
| Refresh           | Re-evaluating `{ThemeResource}` references in a live tree after the overlay changes (§5.1).                                                                                                    |

- **Scoped density in DynamicTree.** The tree ships
  [TreeDensityStyles.xaml](../../../projects/Controls/DynamicTree/src/DynamicTree/TreeDensityStyles.xaml)
  (no WinUI Compact),
  [CompactTreeDensityStyles.xaml](../../../projects/Controls/DynamicTree/src/DynamicTree/CompactTreeDensityStyles.xaml)
  and
  [CompactFilterBarStyles.xaml](../../../projects/Controls/DynamicTree/src/DynamicTree/CompactFilterBarStyles.xaml)
  (both merge WinUI's `DensityStyles/Compact.xaml`). The consumer swaps them per
  tree; the demo does so in
  [TreePresentation.cs](../../../projects/Controls/DemoApp/Tree/TreePresentation.cs).
- **Compact pinned in the editor.** Three files merge WinUI Compact
  unconditionally, so no setting can turn it off:
  [SceneExplorerView.xaml](../../../projects/Oxygen.Editor.SceneExplorer/src/SceneExplorerView.xaml),
  [SceneInspectorView.xaml](../../../projects/Oxygen.Editor.SceneInspector/src/SceneInspectorView.xaml)
  and
  [SceneInspectorStyles.xaml](../../../projects/Oxygen.Editor.SceneInspector/src/SceneInspectorStyles.xaml).
  The editor's main panes therefore look Compact today.
- **Literal sizes in XAML.** Hundreds of literal `FontSize`, `Height`,
  `MinHeight`, `Padding` and `Margin` values sit in WorldEditor, ContentExplorer,
  ProjectExplorer, Editor.Controls and MaterialEditor XAML, and in Docking, Aura
  tabs and several shared controls. `SceneInspectorStyles.xaml` restyles
  `NumberBox`, `VectorBox` and `PropertyCard` with fixed values. The editor also
  carries app-scope `Styles/FontSizes.xaml` and `Styles/Thickness.xaml`.
- **Sizes set in code.** The main shell sets the title-bar rows and icon margin
  once on `Loaded` from `TitleBarOptions.Height`
  ([MainShellView.xaml.cs](../../../projects/Aura/src/MainShellView.xaml.cs)).
  `DynamicTree`'s constructor reads `DynamicTreeItemIndentIncrement` from
  `Application.Resources` once
  ([DynamicTree.cs](../../../projects/Controls/DynamicTree/src/DynamicTree/DynamicTree.cs)),
  and its row, font, icon and indent defaults are dependency-property constants.
  `PropertyCard.MeasureQualifier` uses a fixed `FontSize = 12`
  ([PropertyCard.cs](../../../projects/Oxygen.Editor.Controls/src/Inspector/PropertyCard.cs));
  `NumberBox` places its compact label with fixed margins; the ContentExplorer's
  `TilesLayoutView` adds a fixed `CaptionHeight = 66` to its tile height, and
  `CodeEditorService` builds elements with fixed margins.
- **Application-wide appearance already exists in Aura.**
  [IAppearanceSettings](../../../projects/Aura/src/Settings/IAppearanceSettings.cs)
  and
  [AppearanceSettingsService](../../../projects/Aura/src/Settings/AppearanceSettingsService.cs)
  persist theme settings through `ISettingsService`.
  [AppThemeModeService](../../../projects/Aura/src/Theming/AppThemeModeService.cs)
  applies the theme to every window by setting `RequestedTheme` on its root
  content, reacts to setting changes and to window creation, and applies
  asynchronously through the dispatcher. The shell's Settings menu has a Themes
  radio submenu in
  [MainShellViewModel](../../../projects/Aura/src/MainShellViewModel.cs).
- **Hosts outside a window's visual tree.**
  [PopupMenuHost](../../../projects/Controls/Menus/src/PopupMenuHost.cs) is a
  code-created `Popup`, used by `MenuBar`, `ContextMenu` and `MenuButton`, that
  mirrors the anchor's theme onto its presenter with an explicit
  `RequestedTheme` when it opens. `FlyoutMenuHost`, a `FlyoutBase` host, backs
  `ExpandableMenuBar`.
  [DialogService](../../../projects/Aura/src/Dialogs/DialogService.cs) creates
  `ContentDialog`s and already sets dialog-scoped resources. Three views bypass
  it: `AssetsView` creates a `ContentDialog`, and `HomeView` and
  `NewProjectView` create `NewProjectDialog`, a `ContentDialog` subclass.
  `SceneImportDialogView`, `TextureImportDialogView` and
  `LocalFolderMountDialogView` are content shown through the service.
  `DynamicTreeItem.xaml` declares a `Popup`.
- **Explicit `RequestedTheme` sites:** the window root (`AppThemeModeService`);
  `MenuBar`, which sets it on the active host's root element, that is
  `PopupMenuHost`'s presenter; and `PopupMenuHost` itself, when it opens. Both
  menu sites act on an element inside a code-created `Popup`, outside the window
  tree. `ActualThemeChanged` subscribers: `OutputConsoleView` and `MenuBar`.
- **Startup.** `Program.cs` loads settings synchronously and registers the Aura
  appearance and theme services through DI; `App`'s constructor receives them
  and then calls `InitializeComponent`; navigation to the first window starts
  in `OnLaunched`. `App.xaml` merges `XamlControlsResources` and library
  dictionaries.

## 5. Target Design

### 5.1 Required platform behavior

Each item is a behavior of the pinned Windows App SDK that this design relies
on. Gate V1 (§14) verifies every item as a permanent test; an item that fails
changes the design.

| ID  | Behavior                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P1  | `{ThemeResource}` references are re-evaluated when the theme changes on an ancestor. Merging or removing a dictionary in `Application.Resources.MergedDictionaries` alone changes only elements created afterwards.                                                                                                                                                                                                                                                                                                                             |
| P2  | After the merge or removal, flipping `RequestedTheme` to the opposite of `ActualTheme` and straight back, in the same tick, on a window's root content refreshes that tree in both directions: stock controls, templated library controls, direct `{ThemeResource}` attributes; collapsed, unselected, expanded-then-collapsed and scrolled-out content; `ListView` and `ItemsRepeater` containers, including objects realized before the switch and re-bound to other items after it; and views detached during the switch and attached later. |
| P3  | Replacing `Application.Resources` is not equivalent: some references stay stale. Only add and remove are supported.                                                                                                                                                                                                                                                                                                                                                                                                                             |
| P4  | `{StaticResource}` never updates. Density keys are read with `{ThemeResource}` only.                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| P5  | A key defined in a library's own `Generic.xaml` dictionary shadows an Application-scope overlay. A key in a dictionary at the head of `Application.Resources` does not, and a later overlay overrides it. A missing key is a hard `XamlParseException` that terminates the process when a compiled template needs it.                                                                                                                                                                                                                           |
| P6  | At Application scope, new windows, new and persistent `FlyoutBase` hosts (`Flyout`, `MenuFlyout`, `FlyoutMenuHost`), new and persistent `ToolTip` content, a persistent `ComboBox` dropdown with templated items, and XAML-declared `Popup`s resolve the current density, after the window-root refresh where they already existed.                                                                                                                                                                                                             |
| P7  | A code-created `Popup` kept across a switch is only partly refreshed by the window-root refresh; refreshing its child when it opens fixes it. A new `ContentDialog` is partly stale even with fresh content; merging the overlay into `dialog.Resources` before it shows, or refreshing the dialog after it shows, fixes it.                                                                                                                                                                                                                    |
| P8  | A subtree with its own explicit `RequestedTheme` is not reached by the window-root refresh; refreshing that element itself fixes it.                                                                                                                                                                                                                                                                                                                                                                                                            |
| P9  | A window-root-scoped overlay reaches only that window's own tree: flyouts, popups, dialogs and tooltips stay Comfortable unless each is given the overlay itself. Overlay scope must be Application.                                                                                                                                                                                                                                                                                                                                            |
| P10 | The refresh raises `ActualThemeChanged` twice per affected element with an unchanged theme and unchanged brushes. Handlers must be idempotent.                                                                                                                                                                                                                                                                                                                                                                                                  |
| P11 | WinUI Compact changes some stock sizes (`TextBox`, `ComboBox`, `ListViewItem`) and not others (`Button` and `MenuFlyoutItem` stay 32 DIP). Compact is WinUI Compact plus library keys, never WinUI Compact alone.                                                                                                                                                                                                                                                                                                                               |
| P12 | A control whose instance constructor registers its defaults dictionary at the head of `Application.Resources` gets correct Comfortable and Compact values whether it is first created before or after the overlay is merged, including when its first construction happens while a virtualized list is realizing.                                                                                                                                                                                                                               |

There is no platform API to re-evaluate `{ThemeResource}` on a resource change.
The refresh (P2, P8) is a deliberate workaround, contained in one type (§5.4).

### 5.2 Layers

```text
Oxygen.Editor modules ─ tokens + shared styles/structures ─┐
Oxygen.Editor.Controls (tokens, PropertyCard…)             │ read keys
Aura (setting, service, dialog host)                       │ by name
DroidNet.Controls.* / Docking / Menus (keys, defaults)  ───┘
DroidNet.Density (defaults, overlay, refresh)  ← referenced by Aura, by the
                                                 shared controls and by
                                                 standalone applications
```

Dependencies point downward only. `DroidNet.Density` knows nothing about Aura,
the editor or any control library.

### 5.3 Resource contract for shared controls

- Templates and styles read sizes, padding and font sizes through
  `{ThemeResource}` keys. They contain no literal sizes. WinUI keys are used
  directly (`ListViewItemMinHeight`, `TextControlThemeMinHeight`,
  `ControlContentThemeFontSize`); a library adds a key only where WinUI has none
  (`<Control><Metric>`, for example `DynamicTreeRowHeight`).
- Control properties that today default to density-sensitive constants (the
  `DynamicTree` row, font, icon and indent defaults) are set by the control's
  default style from keys, so a local value still wins and a switch updates
  them.
- Each library ships a **defaults dictionary** (`Density/Defaults.xaml`) with
  every key it uses, and registers it by calling `DensityDefaults.Register(uri)`
  from its controls' **instance** constructors (P12). Keys never live in the
  library's own `Generic.xaml` (P5), and a consumer needs no setup. An
  application may still merge the dictionaries itself. A defaults dictionary
  never redefines a WinUI key: `XamlControlsResources` is merged after the head
  of the list and would win.
- Registration runs on the UI thread, does nothing when there is no
  `Application` (unit tests, design time), and is tracked per
  `Application.Resources` instance in a weak table, not with a static flag: a
  static constructor can run before `App.InitializeComponent` replaces the
  resources, which would drop the registration for good. It inserts at the
  head, skips a source that is already merged (by an earlier registration or
  by the application), and is safe during layout.
- Each library ships a **Compact overlay** (`Density/Compact.xaml`) overriding
  only the keys Compact changes. Libraries do not merge WinUI's
  `DensityStyles/Compact.xaml`; the application's composed overlay does, once,
  and each library README states the pairing.
- Layout-adaptation flags keep their names and meaning and are never set from
  density; each control's documentation states that they are layout
  adaptation, not density.
- Code that must read keys imperatively lives in a shared control library,
  subscribes to `DensityManager.Changed` for its loaded lifetime and re-reads
  them. It never caches a key at construction. `Oxygen.Editor.Controls` is the
  editor's shared control library and may reference `DroidNet.Density` (§13);
  editor modules do not read keys in code. Their code-side sizes (the tile
  caption height, the code-editor margins) become tokens or move into shared
  structures (DEN-04).

### 5.4 The DroidNet.Density package

`DroidNet.Density` is a new top-level module:
`projects/Density/src/Density.csproj` (assembly and package `DroidNet.Density`,
namespace `DroidNet.Density`) with its WinUI test project
`projects/Density/tests/Density.UI.Tests.csproj`. Its README owns the reusable
contract of §5.3 and of this section. Public types:

- `ControlDensity { Comfortable, Compact }`.
- `DensityDefaults`: static. `Register(Uri)` performs the registration of §5.3.
- `DensityManager`: static, because shared controls reach it without
  dependency injection; its state belongs to the current `Application`.
  - `SetOverlays(IReadOnlyList<Uri>)`: the application's composed Compact
    overlay sources.
  - `Apply(ControlDensity)`: adds or removes the overlay dictionaries at
    Application scope (idempotent), increments `Generation`, refreshes every
    registered window root, then raises `Changed` with the density and the
    generation.
  - `Refresh(FrameworkElement)`: flips `RequestedTheme` to the opposite of
    `ActualTheme` and restores the element's own previous value (P2), on the
    element and on every descendant with an explicit `RequestedTheme` (P8),
    found by a tree walk at refresh time. Hosts call it for their own content.
  - `CreateActiveOverlay()`: a new dictionary holding the active overlays, or
    none in Comfortable, for hosts that scope it themselves (dialogs, P7).
  - `RegisterRoot(FrameworkElement)` and `UnregisterRoot(FrameworkElement)`.
- Logging follows the repository's `*.Logs.cs` pattern: each switch logs the
  density, the generation, the overlays changed and the roots refreshed. An
  overlay that fails to load keeps the previous density and is logged as an
  error.

### 5.5 Hosts outside the window tree

- **Code-created `Popup`s** (`PopupMenuHost`, and any other in a shared control)
  refresh their child each time they open when `Generation` changed since they
  last opened, after mirroring the anchor's theme, so the refresh sees the
  final explicit theme. This also reaches the presenter that `MenuBar` themes,
  which a walk from the window root never visits because it sits inside the
  `Popup`. Hosts that build content on every open need nothing.
- **Dialogs** go through `IDialogService`, which merges
  `DensityManager.CreateActiveOverlay()` into `dialog.Resources` before showing,
  the mechanism it already uses for `ContentDialogMaxWidth`. `AssetsView`,
  `HomeView` and `NewProjectView` move onto the service, and `NewProjectDialog`
  becomes a content view shown by it (DEN-02).
- `FlyoutBase` hosts, `ToolTip`, the `ComboBox` dropdown, XAML-declared
  `Popup`s and new windows need nothing (P6). Menus are not virtualized, so a
  refreshed presenter has no recycled items.

### 5.6 Aura

- `IAppearanceSettings.Density`, persisted in the existing appearance section;
  the section's schema version advances. A missing or undefined stored value
  maps to the composition default passed as
  `WithDensityService(defaultDensity)`: Comfortable unless the application
  composes otherwise. The editor composes Compact.
- `AppDensityService`, modeled on `AppThemeModeService`: `ApplyInitial()`
  applies the persisted density synchronously; the application calls it in
  `App`'s constructor right after `InitializeComponent()`, before any window
  exists. It reacts to setting changes on the UI thread and registers and
  unregisters window roots from the window manager's lifecycle events.
  `AuraOptions` gains `WithDensityService(defaultDensity)`.
- A Density radio submenu (Comfortable, Compact) beside Themes in the shell
  Settings menu.
- `DialogService` scopes the active overlay into every dialog (§5.5).

### 5.7 Editor composition and tokens

- One editor overlay dictionary (`Oxygen.Editor/Styles`) merges WinUI's
  `DensityStyles/Compact.xaml` once, each library's Compact overlay and each
  module's Compact overlay; the editor hands its source to
  `DensityManager.SetOverlays` at startup. `App.xaml` merges the editor's own
  token defaults.
- `Oxygen.Editor.Controls` owns the editor tokens (pane padding, section
  spacing, row minimum height, caption size, icon size): a Comfortable defaults
  dictionary and a Compact overlay. Tokens derive from WinUI keys where one
  exists. The existing `Styles/FontSizes.xaml` and `Styles/Thickness.xaml` are
  reconciled into the tokens or kept as non-density constants.
- Reusable structures (pane, section, property row, `PropertyCard`, inspector
  fields) carry their own spacing, so modules compose them and do not set
  `Margin` or `Padding` for layout rhythm.

### 5.8 Window chrome exception

The shell title bar does not follow density. `TitleBarOptions.Height` stays an
explicit per-window design value (40 for main windows, 24 for tool windows in
`WindowDecorationBuilder`), and the shell keeps setting the title-bar rows,
including the document-tab row, and the icon margin from it. Window chrome is
sized per window category and lines up with the system caption buttons, which
the application does not size. Controls hosted inside those rows (menus, tab
items) still follow density within the fixed height.

### 5.9 Why modules do not bypass the density

XAML can always contain a literal, so the guarantee is structural, not absolute:

- Sizes come from keys, tokens and shared structures; the pinned Compact merges
  are deleted; a module has no density setting to override and no access to
  `DroidNet.Density` (§13).
- Dialogs and menus are shared hosts that apply density themselves, so the
  supported way to show either one is density-correct. Nothing prevents a
  module from creating a `ContentDialog` directly; DEN-02 removes the existing
  cases and review keeps new ones out.
- Verification is behavioral (§14): every view is realized in both densities
  and each element governed by a token must measure to that token's value. No
  test or analyzer scans source.
- A literal size is acceptable only when it does not depend on density by
  nature (a glyph tied to a fixed icon, fixed-size media, the engine viewport,
  window chrome). DEN-04 triages each literal as delete (inherit stock), token,
  or invariant.

## 6. Ownership

| Owner                    | Owns                                                                                                        |
| ------------------------ | ----------------------------------------------------------------------------------------------------------- |
| `DroidNet.Density`       | Defaults registration, overlay merge and removal, refresh, generation and `Changed`; the reusable contract. |
| Each shared library      | Its keys, defaults dictionary, Compact overlay, registration calls and README pairing note.                 |
| Aura                     | The `Density` setting, `AppDensityService`, the Settings submenu, dialog overlay scoping, window chrome.    |
| `Oxygen.Editor.Controls` | Editor tokens and the shared editor structures that carry spacing.                                          |
| Editor composition       | The composed overlay, the Compact composition default, the `ApplyInitial()` call.                           |
| Editor modules           | Composing tokens and structures; nothing else about density.                                                |
| Not owned                | Text scaling, DPI, theme selection (Aura's theme service), layout adaptation, the system title-bar buttons. |

## 7. Data Contracts

| Item                         | Contract                                                                                                           |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `AppearanceSettings.Density` | Enum `ControlDensity` in the appearance section; missing or undefined maps to the composition default.             |
| Key names                    | WinUI keys unchanged; library keys `<Control><Metric>`; editor tokens `Editor<Metric>`.                            |
| File placement               | `Density/Defaults.xaml` and `Density/Compact.xaml` in each library and module that owns keys.                      |
| Setting placement            | The Appearance (theme, density) row of [settings architecture §5](./settings-architecture.md#5-setting-placement). |

## 8. Commands, Services, Or Adapters

`AppDensityService` (Aura); `DensityManager` and `DensityDefaults`
(`DroidNet.Density`); `IDialogService` (existing, extended). No scene command
and no `ISceneDocumentCommandService` involvement: density is an editor
preference, never scene data.

## 9. UI Surfaces

A Density radio submenu next to Themes in the shell's Settings menu. V0.1 has
no generic Settings panel
([settings architecture §7](./settings-architecture.md#7-ui-surfaces)).

## 10. Persistence And Round Trip

The setting persists with the appearance section through `ISettingsService`.
At startup the persisted density is applied synchronously before any window is
created, so no window renders in the wrong density first. Nothing is written to
scene documents, history or cooked output.

## 11. Runtime Behavior

Switch sequence, on the UI thread: the setting changes (menu command,
`ISettingsService` or an edit of the watched settings file); `AppDensityService`
calls `DensityManager.Apply`; the overlay is merged or removed; the generation
increments; each registered window root is refreshed with its explicitly themed
descendants; `Changed` fires and imperative readers re-read; popups and dialogs
opened later resolve or refresh themselves (§5.5); the switch is logged.

- The refresh is synchronous per window. A window closing during a switch is
  unregistered by its lifecycle event; a window created after a switch needs no
  refresh.
- Theme and density changes are both serialized on the UI thread, so they
  cannot interleave. `Refresh` restores each element's own `RequestedTheme`, so
  it never overwrites a theme the theme service set.
- Controls that register defaults after the first window exists are correct
  (P12).
- An overlay that fails to load keeps the previous density and logs the error.
  A missing key is a process-terminating failure (P5) that registered defaults
  prevent for shared controls.

## 12. Operation Results And Diagnostics

Switching is logged (§5.4). An overlay load failure is logged as an error and
the previous density is kept. Density has no `OperationResult` domain: it is an
appearance preference, not an authoring operation.

## 13. Dependency Rules

Allowed: shared control libraries (including `Oxygen.Editor.Controls`) →
`DroidNet.Density`; Aura → `DroidNet.Density`; editor modules →
`Oxygen.Editor.Controls` tokens, shared controls and Aura's `IDialogService`;
application composition → everything.

Forbidden: `DroidNet.Density` → any control library, Aura, the editor or any
application type; shared controls → Aura, editor or any application type;
editor modules → `DroidNet.Density` or `AppDensityService`; module-created
`ContentDialog`s and module-owned code-created `Popup`s; windows not created
through Aura's window factory; any code that merges WinUI Compact locally; any
`{StaticResource}` for a density key; any density key in a library's own
`Generic.xaml`.

## 14. Validation Gates

All gates are behavioral; none scans source text. Every gate that reads a size
first asserts that the element is loaded, laid out and visible inside the
window and inside every scroll viewport, and sizes its window so all measured
content fits at the current DPI.

| Gate | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V1   | Platform tests for P1–P12 in `Density.UI.Tests`: each case of §5.1, including collapsed, unselected and scrolled-out content; `ListView` and `ItemsRepeater` containers checked by object identity; detached views; explicitly themed subtrees; code-created and XAML-declared popups; persistent and new `Flyout`, `MenuFlyout` and `FlyoutMenuHost`; persistent and new tooltips; a persistent `ComboBox` dropdown; `AutoSuggestBox` suggestions; `DatePicker`; `CommandBarFlyout`; `TeachingTip`; dialogs; new windows; missing-key failure in a compiled template; both orders of first use for registered defaults; first construction during virtualized realization; registration with no `Application` and after `Application.Resources` is replaced. |
| V2   | Per shared control: realized in both densities, each documented key's effect is measured, and a live switch updates an existing instance and a new instance equally. Controls that read keys in code are covered.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| V3   | Per editor view: realized in both densities and live-switched. Each view's entry lists its elements and the token or key governing each; every such element measures to that value and matches a fresh instance in the target density. Collapsed and virtualized regions are revealed or scrolled into view after the switch.                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| V4   | Aura: a setting change refreshes all open windows; the first window after `ApplyInitial()` needs no refresh; a window created later is correct; the persisted value applies before the first window content; a missing or undefined stored value maps to the composition default; the title-bar rows keep `TitleBarOptions.Height` in both densities.                                                                                                                                                                                                                                                                                                                                                                                                         |
| V5   | Hosts: reused `PopupMenuHost` content (including `MenuBar`'s themed presenter), `DialogService` dialogs and every migrated dialog are correct after a switch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| V6   | Acceptance in the running editor, run by the owner: toggling density updates every open window, pane, dialog, tooltip and menu with no restart, in both directions.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| V7   | Documentation: content coverage, links and formatting per [Oxygen rules](../../oxygen/RULES.md).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |

## 15. Decisions And Rejected Alternatives

### 15.1 Decisions

| Decision                                                                                                                                   | Reason                                                                                            |
| ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------- |
| The refresh is the `RequestedTheme` flip-and-restore, contained in `DensityManager`; it is replaced if the platform adds a refresh API.    | No platform API re-evaluates `{ThemeResource}`; one contained type keeps the workaround isolated. |
| The mechanism ships as the new `DroidNet.Density` package (`projects/Density`), referenced by shared controls and Aura, never the reverse. | Standalone applications take it alone, and its contract stays separate from general utilities.    |
| The overlay scope is Application, not window root.                                                                                         | Only Application scope reaches flyouts, tooltips, popups, dialogs and new windows (P9).           |
| Shared controls self-register their defaults from instance constructors; applications may also merge them explicitly.                      | Zero consumer setup, and a forgotten merge cannot crash an application (P5).                      |
| The application merges WinUI's `DensityStyles/Compact.xaml` once, in its composed overlay; libraries ship only their own overlays.         | One copy of the stock compact resources, controlled by the application.                           |
| Density lives in Aura's appearance settings, in one Appearance (theme, density) row of settings architecture §5.                           | Theme already lives there and already applies to every window.                                    |
| The editor's default density is Compact, passed as `WithDensityService(defaultDensity)`; Aura and the libraries default to Comfortable.    | The editor keeps its current look; libraries stay stock by default.                               |
| The UI is a Density radio submenu beside Themes in the shell Settings menu.                                                                | Same surface as theme; V0.1 has no Settings panel.                                                |
| `NumberBox`, `ToolBar` and `MenuOptions` keep their `IsCompact` names, documented as layout adaptation.                                    | A rename breaks NuGet consumers for no behavioral gain.                                           |
| The shell title bar keeps its explicit `TitleBarOptions.Height` (§5.8).                                                                    | Window chrome is sized per window category and aligned with system caption buttons.               |
| Menus are not virtualized.                                                                                                                 | Refresh-on-open then covers every item of a reused presenter.                                     |
| An overlay load failure is logged as an error and the previous density is kept; there is no `OperationResult` code.                        | Density is an appearance preference, not an authoring operation.                                  |

### 15.2 Rejected alternatives

| Alternative                                               | Why rejected                                                                                                     |
| --------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Recreate views on a switch                                | Loses view state (scroll, expansion, focus, edits in progress) and needs every view to support recreation.       |
| Restart to apply                                          | The mode must apply at once.                                                                                     |
| Swap style identities per control                         | Touches every style and every consumer; the refresh achieves the same with one contained mechanism.              |
| Replace `Application.Resources` instead of add and remove | Leaves references stale (P3).                                                                                    |
| Window-root overlay scope                                 | Does not reach flyouts, tooltips, popups, dialogs or new windows (P9).                                           |
| Keys in each library's own `Generic.xaml`                 | An Application-scope overlay cannot override them (P5).                                                          |
| Explicit consumer merge of defaults as the only delivery  | A forgotten merge terminates the application (P5) and breaks every existing sample application.                  |
| Registration from static constructors                     | Can run before `App.InitializeComponent` replaces the resources and lose the registration permanently.           |
| Per-library merge of WinUI Compact                        | Duplicates the stock dictionary in every library and takes the choice away from the application.                 |
| Runtime guard for module literals                         | It is an enforcement mechanism; the shared vocabulary and behavioral V3 keep modules on the supported path.      |
| Renaming `IsCompact`                                      | NuGet-breaking for consumers; documentation removes the ambiguity.                                               |
| A Settings panel or a toolbar toggle                      | V0.1 has no Settings panel; the shell Settings menu already hosts appearance choices.                            |
| Placing the mechanism inside `Controls.Helpers`           | Couples a cross-cutting contract to an unrelated utility package and forces it on Aura and standalone consumers. |

## 16. Implementation Slices

Each slice lists its work, dependencies and acceptance. Slices validate with
the owning test projects; the owner builds and runs the editor.

| Slice  | Work                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | Depends on     | Acceptance         |
| ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- | ------------------ |
| DEN-01 | `DroidNet.Density` package (`projects/Density`): `ControlDensity`, `DensityManager`, `DensityDefaults`, logging, README with the reusable contract, and `Density.UI.Tests` with the V1 platform tests.                                                                                                                                                                                                                                                                                                                                                                    | —              | V1                 |
| DEN-02 | Aura: `Density` setting and schema version, `AppDensityService`, `WithDensityService(defaultDensity)`, `ApplyInitial()` in `App`'s constructor, Settings submenu, `DialogService` overlay scoping; `AssetsView`, `HomeView` and `NewProjectView` moved onto `IDialogService`, `NewProjectDialog` turned into a content view.                                                                                                                                                                                                                                              | DEN-01         | V4, V5 (dialogs)   |
| DEN-03 | Shared controls, one library at a time: InPlaceEdit (`NumberBox`, `VectorBox`, `ColorBox`), DynamicTree (row, filter bar, item popup, property defaults moved into the default style, the constructor read of `DynamicTreeItemIndentIncrement` removed), ToolBars, Menus (`PopupMenuHost` refresh on open), Docking, Aura tabs, OutputConsole, OutputLog. Each gets keys, defaults, registration, a Compact overlay, its README pairing note and V2. The demo app switches through `DensityManager`; `TreePresentation` and the tree README density section are replaced. | DEN-01         | V2 per library; V5 |
| DEN-04 | Editor: composed overlay and Compact composition default, `Oxygen.Editor.Controls` tokens (`PropertyCard` including its fixed measurement font size, inspector fields, `PropertiesExpander`), reconciliation of `Styles/FontSizes.xaml` and `Styles/Thickness.xaml`, then modules: WorldEditor (Explorer, Inspector, SceneEditor/Viewport, Problems, Cooking), ContentExplorer, ProjectExplorer, MaterialEditor. Delete the three pinned Compact merges; triage every literal in XAML and code-behind; V3 per view.                                                       | DEN-02, DEN-03 | V3 per view        |
| DEN-05 | Acceptance in the running editor; move the shared-control and Aura contracts into the Controls and Aura READMEs; update [PROGRESS](../ui/PROGRESS.md); remove the sections of this LLD that moved to module READMEs.                                                                                                                                                                                                                                                                                                                                                      | DEN-04         | V6, V7             |

The package and its platform tests come first because every later slice relies
on §5.1. Libraries precede the editor so modules have tokens and structures to
compose.
