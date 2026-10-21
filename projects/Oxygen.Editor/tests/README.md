# Oxygen Editor integration tests

The composition root's integration suite: workflows that cross the scene
modules, the content pipeline and the engine. Choose
[an execution tier](#execution-tiers), find a test by
[what it checks](#ownership), and follow the
[fixture rules](#fixtures-and-runtime-cost).

## Execution tiers

| Directory / project suffix                 | Runs                                                                          | Requires                                                             |
| ------------------------------------------ | ----------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| `Integration.UI` / `.Integration.UI.Tests` | Real engine/cooker, publication, saved scene reopening and viewport lifetimes | Installed matching Oxygen SDK and WinUI host                         |
| `Benchmarks.UI` / `.Benchmarks.UI.Tests`   | Large workloads, timing gates and prolonged stress                            | Integration prerequisites and an appropriate measurement environment |

From the repository root:

```powershell
# The editor solution, with this suite.
./projects/Oxygen.Editor/open.cmd -TestScope Integration
```

Build the selected solution before running tests. For a focused command-line
run:

```powershell
traverse Invoke-Tests --start projects/Oxygen.Editor/tests/Integration.UI -- --filter FullyQualifiedName~DocumentViewportLifetimeTests
```

`traverse` follows the supplied path: selecting the entire `tests` directory is
an explicit request to include every program beneath it.

## Ownership

A test belongs here only when it needs several modules and the engine together:
the scene editor's viewports, the scene documents, the live preview, the
content pipeline and the cook. A module's own behavior is tested by that
module.

Native tests check what reaches the engine. They edit through the scene's
authoring, its history and its document, never through a panel's view model or
controls. Folders follow what is checked: Authoring (authored fields reaching
the engine), Documents and SceneEditor (viewport lifetimes and scene
replacement), SceneExplorer (hierarchy synchronization) and Workspace (content
publication, imports, cook repair and recovery).

## Fixtures and runtime cost

Keep a one-use fixture beside its suite. `../testsupport` contains only helpers
with real consumers across classes or programs: the authored fields compared
with the engine's, and native scene/content lifetimes. Reuse the shared DroidNet
UI host and synchronization helpers. Each scenario owns and disposes its state;
fixtures do not share mutable engine instances across independent test cases.

The cooking benchmark prepares its large project once and measures all four
scopes against successive explicit source changes. The catalog benchmark
prepares its scene once and exercises both list and tile layouts. Each variant
retains its assertions and timing measurements. Document-lifetime integration
checks use three transitions; the benchmark project retains the 30-cycle stress
scenario.

Tests must not inject global input or require exclusive use of the desktop. Do
not add screenshots, source archives, hashes or attachments solely to record
that a test ran. Retain data needed by assertions and useful performance
metrics.
