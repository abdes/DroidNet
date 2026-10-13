# Oxygen.Engine Project Layout

Where things go in `projects/Oxygen.Engine`, and the shape every module
follows. Subsystem layouts that go further (for example
[Vortex](vortex/PROJECT-LAYOUT.md)) refine this one and do not contradict it.

## 1. Top level

```text
Oxygen.Engine/
├── src/
│   ├── CMakeLists.txt
│   └── Oxygen/              one folder per module (§2)
├── Examples/                example applications, each with its own test/
├── cmake/                   CMake helpers (module, test, install policy)
├── design/                  engine designs, by area (runtime-core, vortex, …)
├── tools/                   build-tree, presets, oxytools, CLI wrappers
├── profiles/                Conan profiles
├── doxygen/                 API documentation configuration
├── test_package/            Conan package consumer check
├── .clang-tidy              engine-wide checks
├── .clangd                  editor compile adjustments
└── out/, bin/, logs/        generated; never committed
```

## 2. A module

A module is a folder under `src/Oxygen/` with its own `CMakeLists.txt`,
declared with `asap_module_declare` ([cmake/README.md](../cmake/README.md)).
Its dotted name gives the target and alias: `Oxygen.Console` builds
`oxygen-console`, linked as `oxygen::console`.

```text
src/Oxygen/<Module>/
├── CMakeLists.txt           module declaration, sources, add_subdirectory("test")
├── README.md                purpose, API surface, usage
├── api_export.h             OXGN_<X>_API / OXGN_<X>_NDAPI export macros
├── <Type>.h, <Type>.cpp     public API, flat at the module root
├── <Area>/                  optional grouping of a larger module (Types/, Passes/, …)
├── Detail/                  implementation headers that public headers include;
│                            symbols in namespace oxygen::<module>::detail
├── Internal/                implementation files no public header includes;
│                            symbols in namespace oxygen::<module>::internal
├── Docs/                    longer module documentation
├── Schemas/                 JSON Schemas embedded or installed by the module
├── Tools/                   module-owned executables (each a small project)
├── Benchmarks/              opt-in timing programs; not tests
└── test/                    the module's tests (§3)
```

- Public headers include each other as `<Oxygen/<Module>/<Type>.h>`;
  `src/` is the include root.
- `Detail/` and `Internal/` mark intent, not visibility. Whatever a public
  header includes ships in the SDK; check installed headers and
  `PUBLIC FILE_SET`s before relying on a header being private.
- A module may hold sub-modules with their own `CMakeLists.txt` and the
  same shape (`Graphics/Common`, `Graphics/Direct3D12`, `Scripting/Analysis`).

## 3. Tests

Every module's tests live in **one folder named `test`, in lower case**,
directly under the module (or sub-module) folder. No other spelling (`Test`,
`tests`, `Tests`) is used for a test folder.

```text
src/Oxygen/<Module>/test/
├── CMakeLists.txt           link test, then m_gtest_program(...) per program
├── .clang-tidy              inherits the engine checks; relaxes test-only rules
├── Link_test.cpp            links the module alone (target Oxygen.<Module>.LinkTest)
├── <Topic>_test.cpp         test sources, one topic per file
├── Fakes/                   hand-written fakes of interfaces
├── Mocks/                   gmock mocks, only where expectations are checked
├── Fixtures/                shared fixtures and test bases
├── Data/                    test input files
└── <Area>/                  grouping that mirrors the module's own areas
```

- `m_gtest_program(<Name> SOURCES ...)` builds `Oxygen.<Module>.<Name>.Tests`
  and registers it with CTest ([cmake/GTestHelpers.cmake](../cmake/GTestHelpers.cmake)).
- Shared test support for every module (GoogleTest main, `GTest.h`, log
  capture) is the `Oxygen.Testing` module, `src/Oxygen/Testing/`; it is a
  module, not a test folder.
- Prefer plain fakes to gmock: a mock is for checking calls with
  `EXPECT_CALL`, and its macro expansion makes compiling and analysing the
  test much slower.
- The `.clang-tidy` in each `test/` folder starts with
  `InheritParentConfig: true` and turns off only what tests legitimately do:
  fixtures expose protected state to `TEST_F` bodies
  (`cppcoreguidelines-non-private-member-variables-in-classes`), and
  GoogleTest reports an exception escaping a test as a failure
  (`bugprone-exception-escape`, whose call-graph walk dominates analysis time
  of coroutine-heavy tests).
- Tooling recognises test folders by name: oxytidy skips them unless
  `--include-tests` is given, and the CMake source inventory ignores them.

## 4. Examples

```text
Examples/<Example>/
├── CMakeLists.txt
├── README.md
├── main_impl.cpp / sources
└── test/                    same rules as a module's test/ folder
```

`Examples/Content/` holds the example scenes, their sources and the
`play_scene` and cook scripts; `Examples/Common/` holds code shared by
examples.
