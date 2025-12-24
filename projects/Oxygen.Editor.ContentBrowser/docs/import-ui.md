
# Content Browser — Import Dialog (UI Sketch)

This document sketches the Content Browser (CB) **Import** dialog UI, inspired by UE’s asset import experience, but adapted to the current Oxygen pipeline MVP and WinUI 3 best practices.

Primary goals:

- Make **Import** easy (single action) while still exposing a small set of useful options.
- During import, provide **clear progress + live diagnostics**.
- Be resilient: common import failures must not crash the editor.
- Keep the UI state machine unambiguous (what can the user do *right now*?).

Non-goals (for MVP):

- No pre-import “preflight” validation workflow.
- No multi-file batch import UI (can be added later).
- No per-asset advanced importer tuning pages.

---

## Recommended host: `ContentDialog`

Use a WinUI 3 `ContentDialog` for a focused, modal import experience.

- Title: `Import Asset`
- Primary button: `Import`
- Close/secondary button: `Cancel`
- (Optional) `DefaultButton = Primary`

Rationale:

- Matches WinUI interaction expectations for short workflows.
- Keeps keyboard navigation predictable.

---

## Information architecture (mirrors UE’s mental model)

The dialog has three conceptual regions:

1) **Header** (what you are importing + where it will land)
2) **Body** (either import settings *or* diagnostics while importing)
3) **Footer** (Import/Cancel)

This preserves the “pipeline configuration” feeling without overbuilding.

---

## Layout — Idle (pre-import)

### Header (always visible)

- `Import source:` one-line path display (trim/ellipsis in middle)
  - Example: `C:\Users\...\Box.glb`
  - Include a small `Copy` button (icon) to copy the full path to clipboard.
- `Destination:`
  - A folder picker / destination selector representing *the target virtual folder* in the project (default: current folder in CB).
  - A read-only derived label showing the mount point (MVP default: `Content`).
    - Example: `Mount point: Content`

### Body (settings)

Use a compact “settings panel” feel with a **horizontal top navigation** (UE-like) to minimize wasted width:

- Top: settings category selector
  - Use `NavigationView` with `PaneDisplayMode = Top` (or a `TabView`) so categories are a thin horizontal bar.
  - MVP categories:
    - `General`
    - `Logging`

- Below: settings form (for the selected category)
  - Use a `ScrollViewer` + `StackPanel` of `SettingsCard`-like rows (or standard `TextBlock` + controls) to match app styling.
  - Keep controls minimal and mapped to existing pipeline options:
    - `Reimport even if unchanged` (toggle)
    - `Fail fast on first error` (toggle)
    - `Diagnostics verbosity` (ComboBox: `Info`, `Warning`, `Error`)

Notes:

- If importer-specific settings are added later (e.g., glTF material extraction toggles), they become additional sections.
- Avoid free-form text fields unless needed.

### Footer (buttons)

- `Import` (Primary)
- (Optional) `Preview…` (opens an inspection dialog showing the asset structure/contents that will be imported)
- `Cancel` (Close)

---

## Layout — Importing (in-progress)

When the user clicks **Import**:

1) Disable **everything** except `Cancel`.
2) Replace the entire settings body with a diagnostics/progress view.

### Header

Header stays visible and read-only (so the user still sees “what is happening”).

### Body (diagnostics + progress)

Single panel with two stacked areas:

#### Progress bar row

- Determinate `ProgressBar` when `$Completed/$Total` is available.
- Text line beneath (or inline):
  - `Stage: Probe / Import / Build`
  - `Item: /relative/path/to/source.glb` (current item)

#### Diagnostics list

- A list that updates as diagnostics arrive.
- Each row shows:
  - Severity badge (Info/Warning/Error)
  - Message
  - Optional `Source` (path) in a secondary text style if present

Behavior:

- Auto-scroll to newest item while importing.
- Use virtualization-friendly list control for long output (e.g., `ListView`).
- Keep the view non-interactive while importing.

### Footer

- `Import` disabled
- `Preview…` hidden or disabled
- `Cancel` enabled

Cancellation semantics:

- Clicking `Cancel` requests cancellation **and closes the dialog immediately**.
  - Import continues best-effort in the background only if required by infrastructure; UI does not wait.
  - Prefer actually canceling the underlying `CancellationToken`.

---

## Layout — Failed (post-import failure)

If import fails (any error diagnostic):

- Keep the diagnostics view shown (do not switch back to settings automatically).
- Re-enable `Import` so the user can retry (after adjusting settings).
- Keep `Cancel` enabled.

Suggested affordances:

- Top of diagnostics panel shows a compact summary:
  - `Import failed (3 errors, 2 warnings)`
- Provide a `Back to settings` link/button above the diagnostics list (optional but recommended).
  - If you include this, it should just swap the body back to settings without closing the dialog.

---

## Layout — Success (post-import)

If import completes successfully:

- Close the dialog automatically.

Post-close expectations:

- Content Browser refreshes whatever view it is presenting (loose files and/or cooked catalog).
- If the import produced cooked index updates, cooked catalog providers should emit `Changes`.

---

## Interaction model (state machine)

The dialog has three UI states:

- `Idle` → settings visible, `Import` enabled
- `Importing` → diagnostics visible, `Import` disabled, `Cancel` enabled
- `Failed` → diagnostics visible, `Import` enabled, `Cancel` enabled

Transitions:

- `Idle --(Import click)--> Importing`
- `Importing --(Succeeded)--> Close`
- `Importing --(Failed)--> Failed`
- `Importing --(Cancel click)--> Close (request cancellation)`
- `Failed --(Import click)--> Importing`
- `Failed --(Cancel click)--> Close`

---

## Suggested bindings / view-model shape

This is a UI sketch, not an implementation, but it is designed to map cleanly to the existing pipeline contracts.

Inputs:

- `string SourcePath`
- `string ProjectRoot`
- `string DestinationRelativeFolder` (CB-selected folder)

Options (MVP):

- `bool ReimportIfUnchanged`
- `bool FailFast`
- `ImportLogLevel LogLevel`

Runtime UI state:

- `ImportDialogState State` (`Idle | Importing | Failed`)
- `double? ProgressValue` / `int Completed` / `int Total`
- `string Stage`
- `string CurrentItem`
- `ObservableCollection<ImportDiagnosticRow> Diagnostics`

Command behavior:

- `ImportCommand` creates `ImportOptions(Progress: IProgress<ImportProgress>)` and passes a `CancellationToken`.
- While `State == Importing`, the dialog disables all settings controls and leaves only `Cancel` enabled.

---

## Accessibility + WinUI 3 guidelines

- Ensure keyboard flow: Tab from header → body → footer buttons.
- Provide `AutomationProperties.Name` for:
  - Import source path
  - Destination selector
  - Import button / Cancel button
  - Diagnostics list
- Use `InfoBar` only for brief summaries; use the list for detailed diagnostics.
- Ensure high contrast support (use ThemeResources, no hard-coded colors).
