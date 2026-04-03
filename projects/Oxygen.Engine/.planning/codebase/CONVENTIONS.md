# Coding Conventions

**Analysis Date:** 2026-04-03

## Naming Patterns

**Files:**
- Use `PascalCase` for production C++ headers and sources: `src/Oxygen/Serio/Writer.h`, `src/Oxygen/Serio/Writer.cpp`, `Examples/DemoShell/Services/EnvironmentSettingsService.h`.
- Use `_test.cpp` for C++ tests and keep the subject name in the filename: `src/Oxygen/TextWrap/Test/TextWrap_basic_test.cpp`, `src/Oxygen/Loader/Test/GraphicsBackendLoader_test.cpp`, `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`.
- Use `Link_test.cpp` for link/smoke executables registered by CMake: `src/Oxygen/TextWrap/Test/Link_test.cpp`, `src/Oxygen/Scripting/Test/Link_test.cpp`.
- Use `snake_case.py` / `test_*.py` for Python tool code and tests: `src/Oxygen/Core/Tools/BindlessCodeGen/tests/test_validation.py`, `src/Oxygen/Cooker/Tools/PakGen/tests/test_plan_dry_run.py`.
- Keep helper-only test assets in purpose-named support folders: `src/Oxygen/Scripting/Test/Fakes`, `src/Oxygen/Serio/Test/Mocks`.

**Functions:**
- Prefer `UpperCamelCase` for C++ methods and non-trivial free helpers: `SetRuntimeConfig`, `ApplyPendingChanges`, `DirectionFromAzimuthElevation`, `MakeScriptAsset`.
- Prefer trailing-return signatures for non-trivial declarations and definitions: `auto AssetKey::FromString(...) -> Result<AssetKey>` in `src/Oxygen/Data/AssetKey.cpp`, `virtual auto ApplyPendingChanges() -> void` in `Examples/DemoShell/Services/EnvironmentSettingsService.h`.
- Use `snake_case` for Python helpers and tests: `_validate_safe_root`, `test_validate_missing_required_fields` in `src/Oxygen/Cooker/Tools/PakGen/tests/conftest.py` and `src/Oxygen/Core/Tools/BindlessCodeGen/tests/test_validation.py`.

**Variables:**
- Use `snake_case` for locals, parameters, and members: `temp_root_`, `external_result`, `current_length`, `plan_dict`.
- Suffix data members with `_`: `service_` in `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`, `resolver_` in `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`, `messages_` in `src/Oxygen/Testing/ScopedLogCapture.h`.
- Prefix constants and enum values with `k`: `kDegToRad`, `kDefaultSceneCapacity`, `SceneSunCandidateSource::kTagged`.

**Types:**
- Use `PascalCase` for classes, structs, enums, and test fixtures: `EnvironmentSettingsService`, `ScriptSourceResolverTest`, `SceneSunCandidate`, `PanelRegistryError`.
- Keep namespaces lowercase and domain-scoped: `oxygen::examples`, `oxygen::scripting::test`, `oxygen::serio`.
- Keep lifecycle/utility macros uppercase: `OXYGEN_MAKE_NON_COPYABLE`, `OXYGEN_DEFAULT_MOVABLE` in `Examples/DemoShell/Services/EnvironmentSettingsService.h` and `src/Oxygen/Serio/Writer.h`.

## Code Style

**Formatting:**
- Primary formatter: `clang-format` from `.clang-format`.
- Key settings from `.clang-format`:
  - `IndentWidth: 2`
  - `ColumnLimit: 80`
  - `PointerAlignment: Left`
  - `BreakBeforeBraces: WebKit`
  - `NamespaceIndentation: Inner`
  - `IncludeBlocks: Preserve`
- Use `// clang-format off/on` only for data-heavy literals, macro-heavy blocks, or layout-sensitive sections: `src/Oxygen/TextWrap/Test/TextWrap_basic_test.cpp`, `src/Oxygen/Loader/Test/GraphicsBackendLoader_test.cpp`, `src/Oxygen/Serio/Test/Integration_test.cpp`.
- Python editing is aligned to Black in `.vscode/settings.json` with `--line-length 80`.

**Linting:**
- Primary static analysis is `clang-tidy` from `.clang-tidy`.
- Enabled families include `bugprone-*`, `cppcoreguidelines-*`, `google-*`, `modernize-*`, `performance-*`, `portability-*`, and `readability-*`.
- Project-specific relaxations already exist; do not reintroduce banned patterns by hand-fighting them. Examples: `-readability-identifier-length`, `-readability-function-cognitive-complexity`, `-cppcoreguidelines-pro-type-union-access`.
- Pre-commit checks in `.pre-commit-config.yaml` enforce end-of-file fixes, trailing-whitespace cleanup, commit message conventions, and Gersemi formatting for CMake.

## Import Organization

**Order:**
1. Standard library / platform headers: `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`, `src/Oxygen/Data/AssetKey.cpp`
2. Third-party headers: `<glm/...>`, `<gmock/gmock.h>`, `<xxhash.h>`
3. Engine headers via `<Oxygen/...>`
4. File-local quoted headers: `"DemoShell/Services/EnvironmentSettingsService.h"`, `"DemoShell/Services/SettingsService.h"`

**Path Aliases:**
- Use installed-style engine includes rooted at `Oxygen`: `<Oxygen/Serio/Writer.h>`, `<Oxygen/Scripting/Resolver/ScriptSourceResolver.h>`.
- Use quoted project-relative includes for example-local code: `"DemoShell/PanelRegistry.h"`, `"DemoShell/Services/SkyboxService.h"`.
- Do not introduce barrel includes; include concrete headers directly. Current pattern is direct includes such as `src/Oxygen/Serio/Writer.h` and `src/Oxygen/Scripting/Resolver/ScriptSourceResolver.h`.

## Error Handling

**Patterns:**
- Prefer `Result<T>` / `Result<void>` for recoverable library operations: `src/Oxygen/Data/AssetKey.cpp`, `src/Oxygen/Serio/Writer.h`.
- Use `std::expected` where example/UI code returns a domain-specific error enum: `Examples/DemoShell/PanelRegistry.cpp`.
- Reserve exceptions for invalid usage, construction failures, and app-boundary failures: `PanelRegistry` throws `std::invalid_argument`; example entry points catch `CmdLineArgumentsError` and `std::exception` in `Examples/Devices/main_impl.cpp`, `Examples/TexturedCube/main_impl.cpp`, `Examples/Physics/main_impl.cpp`.
- Guard invariants with `CHECK_F` / `DCHECK_F` instead of silent failure: `Examples/MultiView/MainModule.cpp`, `Examples/Async/MainModule.cpp`, `Examples/LightBench/LightScene.cpp`.
- When converting exceptions to result-style APIs, log once and return an error code rather than throwing again: `src/Oxygen/Serio/Writer.h`.

## Logging

**Framework:** `Loguru` through `src/Oxygen/Base/Logging.h`

**Patterns:**
- Use `LOG_F(INFO|WARNING|ERROR, "...", args...)` for operational messages: `Examples/Common/FrameCaptureCli.h`, `Examples/Platform/main_impl.cpp`, `Examples/TexturedCube/TextureLoadingService.cpp`.
- Use `DLOG_F(level, "...")` for verbose debug-only traces: `Examples/MultiView/MainModule.cpp`, `src/Oxygen/Scripting/Resolver/ScriptSourceResolver.cpp`.
- Prefix long-running subsystem messages with a subsystem tag in the message body when context matters: `"TexturedCube: ..."`, `"[MultiView] ..."`, `"Physics: ..."`.
- For tests that need log assertions, capture logs with `oxygen::testing::ScopedLogCapture` from `src/Oxygen/Testing/ScopedLogCapture.h` instead of reaching into logger internals.

## Comments

**When to Comment:**
- Keep the SPDX/license banner at the top of source files; it is present across production and test code such as `src/Oxygen/TextWrap/TextWrap.cpp` and `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`.
- Use concise `//!` / `/*! ... */` Doxygen comments on public APIs and non-obvious helpers: `Examples/DemoShell/Services/EnvironmentSettingsService.h`, `src/Oxygen/Serio/Writer.h`, `src/Oxygen/Serio/Test/Integration_test.cpp`.
- Use short inline comments to explain non-obvious test intent, binary layouts, or temporary formatting escapes, not to narrate obvious code: `src/Oxygen/Serio/Test/Integration_test.cpp`, `src/Oxygen/Loader/Test/GraphicsBackendLoader_test.cpp`.

**JSDoc/TSDoc:**
- Not applicable.
- The equivalent documentation style is Doxygen-flavored C++ comments (`//!`, `/*! ... */`) in headers and occasionally tests.

## Function Design

**Size:** Prefer small file-local helpers in anonymous namespaces for reusable substeps, then keep orchestration in the owning class/module. Examples: helper cluster in `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`, smaller focused helpers in `src/Oxygen/Data/AssetKey.cpp`.

**Parameters:** Pass lightweight read-only inputs as `std::string_view`, `const&`, spans, or small value types; examples include `AssetKey::FromString(std::string_view)` in `src/Oxygen/Data/AssetKey.cpp` and `WriteFile(const std::filesystem::path&, std::string_view)` in `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`.

**Return Values:**
- Prefer explicit return types with trailing syntax for C++ APIs.
- Mark query-style methods `[[nodiscard]]`: `HasScene`, `GetEpoch`, `Position`, `Flush` in `Examples/DemoShell/Services/EnvironmentSettingsService.h` and `src/Oxygen/Serio/Writer.h`.
- Return domain objects or status wrappers instead of output parameters where possible: `Result<AssetKey>`, `std::expected<void, PanelRegistryError>`.

## Module Design

**Exports:** Keep one primary type/focus per header/source pair and export through concrete headers: `src/Oxygen/Serio/Writer.h`, `src/Oxygen/Scripting/Resolver/ScriptSourceResolver.h`, `Examples/DemoShell/Services/EnvironmentSettingsService.h`.

**Barrel Files:** Not used. Include concrete module headers directly rather than introducing umbrella headers.

**Additional practical rules:**
- Put private implementation helpers in anonymous namespaces inside `.cpp` files: `src/Oxygen/Data/AssetKey.cpp`, `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`.
- Keep tests in the same domain namespace as the module plus a `test`/`testing` suffix namespace: `oxygen::scripting::test`, `oxygen::examples::testing`.
- Keep CMake formatted with lowercase commands and vertically aligned argument blocks as seen in `cmake/GTestHelpers.cmake`, `src/Oxygen/Scripting/Test/CMakeLists.txt`, and `src/Oxygen/Testing/CMakeLists.txt`.

---

*Convention analysis: 2026-04-03*
