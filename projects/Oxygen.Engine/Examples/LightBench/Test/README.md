# LightBench Test Suite

The LightBench test suite validates photometric accuracy, settings
serialization, console command bindings, and user interface workflows for the
LightBench example application.

Testing is divided into two distinct layers: **Native C++ Tests** (using
Google Test) for data and rendering verification, and **UI Automation Tests**
(using ImGui Test Engine) for in-app interaction coverage.

---

## Test Organization

### Native Tests (Google Test)

The native executable is `Oxygen.Examples.LightBench.Tests`. Settings, file and
console tests run on the CPU; reference rendering tests use a D3D12 device and
GPU readbacks.

| Test File                                              | Coverage & Responsibilities                                                                                                                                              |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [`Reference_test.cpp`](Reference_test.cpp)             | Photometric accuracy (GGX shading, material response, calibrated card luminance), exposure mode adaptation, directional/point/spot light falloff, and clipping behavior. |
| [`Settings_test.cpp`](Settings_test.cpp)               | Settings schema validation, parameter constraints, coupled-invalid configuration rules, and preset default values.                                                       |
| [`SettingsFile_test.cpp`](SettingsFile_test.cpp)       | Atomic file replacement, missing/corrupted/oversized files, failed replacement preserving existing data, and retry.                                                      |
| [`ConsoleBindings_test.cpp`](ConsoleBindings_test.cpp) | Console command registration, argument parsing, execution dispatch, and rollback handling on registration conflicts.                                                     |

### UI Automation Tests (ImGui Test Engine)

In-app automation tests in
[`Widgets_ui.cpp`](Widgets_ui.cpp)
that run inside the live LightBench application frame loop.

| Test Case                     | Description                                                                                                                                                             |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `preset_reset`                | Selects presets from the top scenario bar, validates non-default dirty states, and verifies resetting back to defaults.                                                 |
| `numeric_commit_cancel`       | Tests ImGui panel input field interactions: commit via Enter/Tab/click, cancellation via Escape, and mouse drag adjustments.                                            |
| `mode_curve_output`           | Verifies post-process controls including exposure mode selection, ISO input, compensation curve editing, and gamma bounds checking.                                     |
| `authored_mask_toggle`        | Tests asynchronous loading of custom texture assets on the engine thread and verifies metering mask status toggling (`Ready` / `Absent`).                               |
| `save_load_and_panel_return`  | Validates end-to-end JSON file saving and loading through the UI panel, state preservation across side-bar navigation, and camera extents retention.                    |
| `console_preset_and_settings` | Opens the interactive console overlay (grave accent key), dispatches `pp.exposure` and `lightbench.*` commands, and checks state updates and invalid argument handling. |

---

## Running Tests

### Running Native Tests

Run the Google Test executable using `oxyrun.ps1`:

```powershell
# Run Debug tests
./tools/cli/oxyrun.ps1 Oxygen.Examples.LightBench.Tests -BuildTree build-ninja -Config Debug

# Run Release tests
./tools/cli/oxyrun.ps1 Oxygen.Examples.LightBench.Tests -BuildTree build-ninja -Config Release
```

### Running UI Automation Tests

The VS Code PowerShell profile provides the `oxy-ui-tests` alias for the runner.

`generate-builds.ps1` enables UI tests by default for development, including
optimized Release builds. Configure dependencies and rebuild the applications:

```powershell
./tools/generate-builds.ps1 profiles/windows-msvc.ini -Generator Ninja -WithTracy -NoClean
cmake --build out/build-tracy-ninja --config Release --target oxygen-examples-lightbench oxygen-examples-texturedcube --parallel 8
./tools/cli/oxy-ui-tests.ps1 -Demo LightBench -BuildTree build-tracy-ninja -Config Release
./tools/cli/oxy-ui-tests.ps1 -Demo TexturedCube -BuildTree build-tracy-ninja -Config Release
```

Build Debug and use `-Config Debug` for assertion-enabled runs. Select a case
with `-Filter authored_mask_toggle`; filters match test names or categories.
Use `-Output <new-directory>` to choose the report location. Each run writes
JUnit XML, logs, launch metadata and a final client image under `out/ui-tests`.
Failed cases get client images when the window is visible.

For a final-release build, configure with `-UiTests:$false` and rebuild.
Dependency deployment follows the script's usual settings in either mode.

---

## Architecture Notes

- **Threading Model**: In
  [`Widgets_ui.cpp`](Widgets_ui.cpp),
  loader setup and engine inspection run in `GuiFunc` on the main engine thread,
  while `TestFunc` runs cooperatively on the ImGui Test Engine driver thread.
- **Resource Mounting**: Fixture assets and settings are written into each run directory.
  Mask tests mount real cooked textures through `AssetLoader`.
