# WorldEditor tests

Choose [an execution tier](#execution-tiers), use the [owning feature](#ownership)
to find a test, and follow the [fixture rules](#fixtures-and-runtime-cost).

## Execution tiers

| Directory / project suffix                 | Runs                                                                                   | Requires                                                             |
| ------------------------------------------ | -------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| `Unit` / `.Unit.Tests`                     | Commands, history, synchronization contracts, policies and settings                    | Ordinary .NET test host                                              |
| `Unit.UI` / `.Unit.UI.Tests`               | WinUI controls, bindings and dispatcher-bound view models with controlled dependencies | Shared unpackaged WinUI test host                                    |
| `Integration.UI` / `.Integration.UI.Tests` | Real engine/cooker, publication, saved scene reopening and viewport lifetimes          | Installed matching Oxygen SDK and WinUI host                         |
| `Benchmarks.UI` / `.Benchmarks.UI.Tests`   | Large workloads, timing gates, prolonged stress and explicit image qualification       | Integration prerequisites and an appropriate measurement environment |

Every UI-hosted project includes `.UI` in its name. A dispatcher-bound view-model
test belongs in `Unit.UI` even when it does not display a control. It must not
manufacture an empty window just to obtain a dispatcher.

From the repository root:

```powershell
# All test programs, preserving the normal open.cmd workflow.
./projects/Oxygen.Editor.WorldEditor/open.cmd

# Optional filtered solution with an explicit separate output path.
./tooling/GenerateSolution.ps1 -Scope projects/Oxygen.Editor.WorldEditor -TestScope Unit -SolutionPath artifacts/WorldEditor.Unit.sln -Launch
```

Build the selected solution before running tests. Test Explorer can group by
project, namespace and class. For a focused command-line run:

```powershell
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit -- --filter FullyQualifiedName~ColorHistoryTests
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit.UI -- --filter FullyQualifiedName~NumericGesturesTests
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Integration.UI -- --filter FullyQualifiedName~DocumentViewportLifetimeTests
```

`traverse` follows the supplied path: selecting the entire `tests` directory is
an explicit request to include every program beneath it.

## Ownership

Folders and namespaces follow Documents, Inspector, SceneExplorer, SceneEditor,
Services, Workspace, Cooking and Inspection. Classes identify the behavior being
tested; unrelated features do not share a partial test class.

Content Browser filtering, navigation, asset presentation and import-dialog tests
live under `Oxygen.Editor.ContentBrowser/tests`. Its controls use the separate
`Oxygen.Editor.ContentBrowser.UI.Tests` project. Material transaction/history
coverage lives under `Oxygen.Editor.MaterialEditor/tests`.

Cross-module workflows stay in WorldEditor integration tests when WorldEditor
owns the coordination, such as publishing cooked material changes to an open
scene or replacing an active viewport during document navigation.

## Fixtures and runtime cost

Keep a one-use fixture beside its suite. `../testsupport` contains only helpers
with real consumers across classes or programs: authoring state, inspector
control operations and native scene/content lifetimes. Reuse the shared DroidNet
UI host and synchronization helpers. Each scenario owns and disposes its state;
fixtures do not share mutable engine instances across independent test cases.

The cooking benchmark prepares its large project once and measures all four
scopes against successive explicit source changes. The browser benchmark prepares
its scene once and exercises both list and tile layouts. Each variant retains its
assertions and timing measurements. Document-lifetime integration checks use three
transitions; the benchmark project retains the 30-cycle stress scenario.

Tests must not inject global input or require exclusive use of the desktop.
Do not add screenshots, source archives, hashes or attachments solely to record
that a test ran. Retain data needed by assertions and useful performance metrics.
Image qualification may emit a GPU capture and its inputs only when explicitly
requested through the `IblCapture` test parameter.
