# EX08.2 widget regression automation

Status: validated (2026-09-25). Structured commit delivery was authorized after user review.

Use the existing ImGui 1.92.5 Conan package's matching Test Engine option.
`generate-builds.ps1` enables `UiTests` by default for development in Debug,
Release and RelWithDebInfo. Final-release generation uses `-UiTests:$false`.
The script forwards the choice through Conan to `OXYGEN_BUILD_UI_TESTS`; direct
Conan/package defaults remain false and can be overridden with `-o ui_tests=True`.
Build configuration and shipping intent are separate choices.

The test session belongs to the demo layer. It attaches to the renderer's
existing ImGui context and invokes Test Engine's post-swap work at frame end,
after presentation. It stops and unregisters its context hooks before the demo
is destroyed. No renderer/shader instrumentation or measurement system is added.

Tests operate the actual application widgets. They may read the real scene and
settings owners as assertions, but must not replace the input under test with
direct setters. Setup may select a known preset or mount a deterministic fixture.
The required workflows remain numeric entry (Enter/Tab/focus loss/Escape), live
drag, mode changes, mask/curve edits, preset reset and save/load, and TexturedCube
assignment surviving panel return. Reports must distinguish queued edits from
applied application state. Native numerical qualification remains authoritative.

Use the existing Tracy Ninja tree for explicit Release UI qualification, keeping
the ordinary non-Tracy tree available for the user's review. UI test results are
functional checks, not performance measurements. Do not create another build
tree. Record build isolation and restore the test tree to its ordinary option
after qualification. Structured commits were authorized after qualification and review.

The runner must preserve user settings, use an explicit report directory, return
failure for missing/failed tests, and provide per-test logs plus failure images.
No image comparison tolerance or renderer baseline changes belong to this work.

## Results

| Qualified check                                   | Debug | Release |
| ------------------------------------------------- | ----: | ------: |
| LightBench real widget workflows                  |   6/6 |     6/6 |
| TexturedCube assignment, panel return and refresh |   1/1 |     1/1 |
| Changed LightBench CPU tests after migration      |   7/7 |     7/7 |

LightBench covers preset/dirty/reset and the scrollbar-free popup; Enter, Tab,
Escape, focus loss and live drag; physical-camera/Auto modes, curve application
and reset, invalid/valid gamma; real mask Ready → Absent → Ready; complete
Load/Save/Reset/Load and unchanged camera framing on panel return; and typed
console edits, atomic rejection, stale targets, preset application and reset.

The [test migration review](EX082-test-migration.md)
keeps numerical, schema and file-failure checks native while moving simulated
application reset and preset application to real widgets. Nine unchanged
reference/rendering cases retain their existing proof and were not rerun.

The four new test translation units are clang-tidy clean in Release. All 26
existing preset-tool tests pass. Both existing Ninja trees were rebuilt in
Debug/Release with UI instrumentation **off** after qualification. Their compile
databases contain no Test Engine defines or UI test sources, and their ordinary
LightBench/TexturedCube executables contain no test-session environment marker.
The runner rejects an ordinary tree before creating output or launching a demo.

[Durable results, source/binary identities and raw records](validation/ex082-20260925/results.json)
preserve the successful runs and relevant failed development attempts. Runner UTC
times are authoritative; upstream JUnit formats its monotonic clock as an epoch.
UI qualification used the instrumented Tracy tree and is not a performance claim.

Integration corrections found during development were in the test harness:
ImGui 1.92.5's combo needs an exact scoped ID; the Conan package does not enable
its optional PNG capture backend; and AssetLoader setup/renderer inspection must
run in the engine-thread `GuiFunc`, not the cooperative driver. Native BMP
failure capture is implemented and was visually verified. An occluded client
produces an explicit unavailable-image record, never a misleading screenshot.
These findings did not require changing renderer or asset-lifetime contracts.

## Enable, run, restore

Use the existing tree. This opt-in changes the ImGui dependency variant, so
configure through Conan and rebuild the applications; a CMake-only toggle is
rejected when it disagrees with the dependency variant. Dependency deployment
follows the generator's usual settings in both development and final-release mode.

```powershell
./tools/generate-builds.ps1 profiles/windows-msvc.ini -Generator Ninja -WithTracy -NoClean
cmake --build out/build-tracy-ninja --config Release --target oxygen-examples-lightbench oxygen-examples-texturedcube --parallel 8
./tools/cli/oxy-ui-tests.ps1 -Demo LightBench -BuildTree build-tracy-ninja -Config Release
./tools/cli/oxy-ui-tests.ps1 -Demo TexturedCube -BuildTree build-tracy-ninja -Config Release
```

Use `-Config Debug` after building Debug for assertion-enabled checks.
`-Filter authored_mask_toggle` selects a test by name; Test Engine filters names
or categories, not slash-separated paths. A filter matching no tests fails.
Each run requires a new report directory (`-Output` can override its default).
The app suppresses preference writes, the suite uses private fixture/settings
paths, and the runner closes only its own process on timeout or interruption.
Reports are standard JUnit plus logs/launch metadata, not a measurement schema.
Client images are BMPs; an unavailable capture is recorded explicitly.

For final-release builds, pass `-UiTests:$false`, keep `-NoClean`, and rebuild
the same targets. Console commands remain available; the UI driver, fixtures,
hooks and capture code are excluded.

The development-default policy supersedes the earlier opt-in workflow. The
qualification records above describe their original runs with instrumentation
enabled and subsequent restoration with it disabled. The initial policy edit
did not regenerate existing trees.

The updated generator passes all 26 tooling tests, including default-on and
explicit True/False propagation across configurations, plus dependency deployment
in both modes. [Policy-change evidence](validation/ex082-20260925/development-default/result.json)
is separate from the earlier runtime qualification records. No binaries were
rebuilt and no capture or benchmark was repeated for this configuration change.

The subsequent development-environment repair ran the actual generator with
`-Generator Ninja -NoClean`. Conan and CMake configured `out/build-ninja` with
UI tests enabled in Debug, Release and RelWithDebInfo. Its compile database has
nine UI-source entries with the required defines and matching Test Engine headers.
VS Code's installed clangd 22.1.6 checks all three UI translation units with zero
errors; the user confirmed the editor issue resolved. This repair configured
the tree without rebuilding its binaries. [Environment evidence](validation/ex082-20260925/development-ninja/result.json).
