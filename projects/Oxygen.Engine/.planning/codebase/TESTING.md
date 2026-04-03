# Testing Patterns

**Analysis Date:** 2026-04-03

## Test Framework

**Runner:**
- C++: GoogleTest + GoogleMock via `find_package(GTest REQUIRED CONFIG)` in `src/Oxygen/Testing/CMakeLists.txt`, `src/Oxygen/TextWrap/Test/CMakeLists.txt`, and `src/Oxygen/Scripting/Test/CMakeLists.txt`.
- C++ registration/config: `cmake/GTestHelpers.cmake`
- Python: `pytest` configured by `pytest.ini`

**Assertion Library:**
- C++: GoogleTest / GoogleMock matchers and expectations via `src/Oxygen/Testing/GTest.h`
- Python: plain `assert`, `pytest.raises`, `pytest.approx`, `pytest.mark.parametrize`

**Run Commands:**
```bash
cmake --preset <preset>                                    # configure native tests
ctest --test-dir out/build-<preset> --output-on-failure    # run discovered C++ tests
pytest src/Oxygen/Core/Tools/BindlessCodeGen/tests         # run BindlessCodeGen Python tests
pytest src/Oxygen/Cooker/Tools/PakGen/tests                # run PakGen Python tests
```

## Test File Organization

**Location:**
- C++ tests are module-local under `Test` directories beside production code: `src/Oxygen/TextWrap/Test`, `src/Oxygen/Scripting/Test`, `src/Oxygen/Loader/Test`, `src/Oxygen/Graphics/Headless/Test`.
- Example-level tests live under the example: `Examples/DemoShell/Test`.
- Python tests stay next to the tool/package they validate: `src/Oxygen/Core/Tools/BindlessCodeGen/tests`, `src/Oxygen/Cooker/Tools/PakGen/tests`.

**Naming:**
- C++ behavior tests use `*_test.cpp`: `TextWrap_basic_test.cpp`, `GraphicsBackendLoader_test.cpp`, `EnvironmentSettingsService_test.cpp`.
- C++ link/smoke tests use `Link_test.cpp`: `src/Oxygen/Scripting/Test/Link_test.cpp`.
- Python tests use `test_*.py`: `test_validation.py`, `test_plan_dry_run.py`, `test_collision_shape_pack_contract.py`.

**Structure:**
```text
src/Oxygen/<Module>/Test/
  CMakeLists.txt
  Link_test.cpp
  <Feature>_test.cpp
  Fakes/ or Mocks/            # when module tests need doubles

Examples/<Example>/Test/
  CMakeLists.txt
  <Feature>_test.cpp

src/Oxygen/<Tool>/tests/
  conftest.py                 # shared pytest fixtures when needed
  spec_fixtures.py            # reusable test data builders
  test_*.py
```

## Test Structure

**Suite Organization:**
```typescript
class <Feature>Test : public ::testing::Test {
protected:
  void SetUp() override;
  // helper builders / fake dependencies
};

NOLINT_TEST_F(<Feature>Test, DoesSomethingObservable)
{
  // Arrange
  // Act
  // Assert
}
```

Observed in `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`, `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`, and `src/Oxygen/Base/Test/ComError_test.cpp`.

**Patterns:**
- Use `NOLINT_TEST`, `NOLINT_TEST_F`, `NOLINT_TEST_P`, and related wrappers from `src/Oxygen/Testing/GTest.h` to suppress macro-related lint noise consistently.
- Keep helpers and builders inside the test fixture or an anonymous namespace instead of global utility files unless reused across many suites: `MakeScene`, `CreateDirectionalLightNode` in `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`; `MakeScriptAsset` in `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`.
- Prefer clear arrange/act/assert spacing with short explanatory comments in larger tests: `src/Oxygen/Serio/Test/Integration_test.cpp`, `src/Oxygen/Base/Test/EnumAsIndex_test.cpp`.
- Use dedicated fixture headers only when many suites share the same setup: `src/Oxygen/Scripting/Test/ScriptingModule_test_fixture.h`.

## Mocking

**Framework:** GoogleMock for C++; pytest uses fixtures and real values more than mocking.

**Patterns:**
```typescript
class MockPlatformServices : public PlatformServices {
public:
  MOCK_METHOD(std::string, GetExecutableDirectory, (), (const, override));
};

ON_CALL(*platform, GetExecutableDirectory())
  .WillByDefault(testing::Return("C:\\FakePath\\"));
EXPECT_CALL(*platform, CloseModule(testing::_)).Times(1);
```

Pattern shown in `src/Oxygen/Loader/Test/GraphicsBackendLoader_test.cpp`. Additional mock-heavy suites appear in `src/Oxygen/Base/Test/StateMachine_test.cpp`.

**What to Mock:**
- Mock interfaces, boundary services, loaders, and callbacks: `PlatformServices` in `src/Oxygen/Loader/Test/GraphicsBackendLoader_test.cpp`, action/state interfaces in `src/Oxygen/Base/Test/StateMachine_test.cpp`.
- Use fake engines/services when a reusable behavioral double is simpler than mock expectations: `src/Oxygen/Scripting/Test/Fakes/FakeAsyncEngine.h`, `src/Oxygen/Scripting/Test/Fakes/FakeScriptCompilationService.h`.

**What NOT to Mock:**
- Do not mock simple value types, containers, or binary payloads when an in-memory real instance is cheap. Use real `MemoryStream`, vectors, scenes, and temp files as in `src/Oxygen/Serio/Test/Integration_test.cpp`, `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`, and `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`.
- Python tests favor real serialized buffers and temp directories over monkeypatched internals unless the dependency boundary is external (`monkeypatch` only appears for targeted cases in `src/Oxygen/Core/Tools/BindlessCodeGen/tests/test_validation.py`).

## Fixtures and Factories

**Test Data:**
```typescript
auto scene = MakeScene("DemoShell.InjectSyntheticSun");
auto asset = MakeScriptAsset("runtime/fallback", ScriptAssetResourceIndices {}, ...);
doc = create_full_example_document()
```

**Location:**
- C++ fixture helpers stay near the suites that own them: `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`, `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp`.
- Shared C++ fixtures live in dedicated support headers: `src/Oxygen/Scripting/Test/ScriptingModule_test_fixture.h`.
- Python reusable document builders live in `src/Oxygen/Core/Tools/BindlessCodeGen/tests/spec_fixtures.py`.
- Python temp-path safety and cleanup behavior live in `src/Oxygen/Cooker/Tools/PakGen/tests/conftest.py`.

## Coverage

**Requirements:** No repository-enforced percentage target detected.

**View Coverage:**
```bash
cmake --preset <preset> -DOXYGEN_WITH_COVERAGE=ON
ctest --test-dir out/build-<preset> --output-on-failure
```

Coverage signal exists, but it is opt-in:
- Top-level toggle: `OXYGEN_WITH_COVERAGE` in `CMakeLists.txt`
- Generic `--coverage` flag injection for GTest executables: `cmake/GTestHelpers.cmake`
- Additional explicit coverage flags for functional coroutine tests: `src/Oxygen/OxCo/Test/CMakeLists.txt`

## Test Types

**Unit Tests:**
- Dominant test type.
- Validate pure/domain behavior per module: `src/Oxygen/TextWrap/Test`, `src/Oxygen/Base/Test`, `src/Oxygen/Serio/Test`, `src/Oxygen/Loader/Test`.

**Integration Tests:**
- Present where real subsystems or file/binary round-trips matter.
- Examples:
  - `src/Oxygen/Serio/Test/Integration_test.cpp` for end-to-end serialization/deserialization
  - `src/Oxygen/Scripting/Test/ScriptSourceResolver_test.cpp` for filesystem-backed script resolution
  - `Examples/DemoShell/Test/SceneLoaderService_phase4_test.cpp` for service orchestration
  - `src/Oxygen/Cooker/Tools/PakGen/tests/test_plan_dry_run.py` for plan/build parity

**E2E Tests:**
- No standalone browser/UI E2E framework detected.
- Closest equivalents are example/service orchestration suites under `Examples/DemoShell/Test` and executable link tests registered with `add_test(...)`.

## Common Patterns

**Async Testing:**
```typescript
ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> { engine.get() }));
ASSERT_TRUE(hook_result.ok) << hook_result.message;
EXPECT_FALSE(context.HasErrors());
```

Seen in `src/Oxygen/Scripting/Test/Bindings_log_test.cpp`, `src/Oxygen/Scripting/Test/Bindings_physics_body_test.cpp`, and `src/Oxygen/Scripting/Test/ScriptingModule_HotReload_test.cpp`.

**Error Testing:**
```typescript
EXPECT_THROW(..., std::invalid_argument);
EXPECT_DEATH_IF_SUPPORTED(..., "");
with pytest.raises(ValueError, match="validation failed"):
```

Seen in `src/Oxygen/Clap/Test/ParseValue_test.cpp`, `src/Oxygen/Base/Test/EnumAsIndex_test.cpp`, `src/Oxygen/PhysicsModule/Test/PhysicsModule_sync_test.cpp`, `src/Oxygen/Core/Tools/BindlessCodeGen/tests/test_validation.py`, and `src/Oxygen/Cooker/Tools/PakGen/tests/test_collision_shape_pack_contract.py`.

**Additional observed patterns:**
- Parameterized C++ suites are common for combinatorial behavior: `src/Oxygen/TextWrap/Test/Tokenizer_test.cpp`, `src/Oxygen/Clap/Test/ParseValue_test.cpp`.
- Typed C++ suites are used for template/integer-family coverage: `src/Oxygen/Base/Test/ComError_test.cpp`, `src/Oxygen/Serio/Test/Writer_test.cpp`.
- Matcher-heavy assertions (`EXPECT_THAT`, `Eq`, `HasSubstr`, `NotNull`) are preferred when they read better than raw boolean checks: `src/Oxygen/TextWrap/Test/TextWrap_basic_test.cpp`, `Examples/DemoShell/Test/GraphicsToolingCli_test.cpp`.
- Log assertions should use `ScopedLogCapture` rather than custom logger hooks: `Examples/DemoShell/Test/EnvironmentSettingsService_test.cpp`.
- Python tests prefer explicit helper functions and literal fixture dictionaries over opaque factories when binary/layout details matter: `src/Oxygen/Cooker/Tools/PakGen/tests/test_collision_shape_pack_contract.py`.

---

*Testing analysis: 2026-04-03*
