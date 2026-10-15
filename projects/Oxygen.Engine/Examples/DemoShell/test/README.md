# DemoShell tests

## Native tests

Each `*_test.cpp` is a GoogleTest program named
`Oxygen.Examples.DemoShell.<Name>.Tests`. Build and run one with the engine
workflow, for example:

```powershell
cmake --build --preset oxygen-ninja-debug --target Oxygen.Examples.DemoShell.FileBrowser.Tests
./tools/cli/oxyrun.ps1 Oxygen.Examples.DemoShell.FileBrowser.Tests -Preset oxygen-ninja-debug -NoBuild
```

## UI tests

The DemoShell's own widgets and panels are tested with the ImGui Test Engine in
a dedicated runner, `Oxygen.Examples.DemoShell.UiRunner`. It hosts the
DemoShell on an empty scene with one camera, so no example application is
involved. It exists only when UI tests are enabled
(`OXYGEN_BUILD_UI_TESTS`).

- `UiRunnerMain.cpp` and `UiRunnerModule.*` are the runner's entry point and
  application module; `UiRunnerModule::RegisterUiTests` lists the suites.
- `FileBrowser_ui.cpp` holds the file browser widget tests. Each test owns its
  widget and a scratch directory, so tests need no application state.
- `UiTestSession.*` and `UiTestAssets.*` are shared by the runner and by the
  examples that run their own UI tests.

```powershell
cmake --build --preset oxygen-ninja-debug --target oxygen-examples-demoshell-ui-runner
./tools/cli/oxy-ui-tests.ps1 -Demo DemoShell -BuildTree build-ninja -Config Debug
```

Select a suite or case with `-Filter filebrowser`. Add a suite by writing its
registration function next to `FileBrowser_ui.cpp` and calling it from
`UiRunnerModule::RegisterUiTests`. Tests that belong to an example stay in
that example.
