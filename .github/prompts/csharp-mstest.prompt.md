---
agent: "agent"
description: "Write focused MSTest tests using DroidNet tooling and fixtures"
---

# DroidNet MSTest tests

## Project setup

- Follow root/module `AGENTS.md` and the owning suite. Managed tests use
  `MSTest.Sdk`, pinned by `global.json`, not hand-added MSTest runner packages or
  legacy Coverlet flags.
- Project names end in `.Tests`; WinUI unit tests end in `.UI.Tests`, integration
  tests in `.Integration.UI.Tests`.
- Reuse `projects/TestHelpers` and the relevant templates under `tooling/samples`.
  Put versions in `Directory.packages.props`. Do not introduce another mocking
  library or a DI container just for a test.

## What a test asserts

- **The contract, not the current output.** Derive every expected value from the
  written contract (LLD, plan, API docs) and quote the rule in the test or its
  name. A test that pins wrong output is part of the bug: rewrite it and fix the
  producer. Never add a converter at the consumer to make a test pass. Fixtures
  and fake data are contract-shaped too.
- **Codes, not messages.** Never assert diagnostic or error text; assert contract
  codes and ids. Reuse `Oxygen.Managed.Core.Diagnostics` (`DiagnosticCodes`
  `OXE.*`, per-area `*DiagnosticCodes`). A refusal without a code is a contract
  gap to raise, not something to match by string.
- **Owned literals come from their owner.** Never hard-code format versions,
  schema ids, mount names, extensions or other literals a production type owns;
  read the schema or constant. When a format or id changes, migrate every fixture
  that builds those records in the same change, searching by record type rather
  than by compile errors.
- **Every new behavior gets its test in the same change:** each new record,
  table, diagnostic, public method or code path. Before calling a slice done,
  check that each new public behavior is covered.
- Assert observable behavior with the suite's AwesomeAssertions/MSTest patterns.
  Add assertion messages only for missing context. Do not assert private calls or
  mock interactions that lock a test to an implementation instead of its contract.

## Structure and naming

- Use `[TestClass]`, `[TestMethod]` and scenario-focused names such as
  `Method_Scenario_ExpectedBehavior`.
- Use `[DataRow]`/`[DynamicData]` for meaningful variations; lifecycle attributes
  only for shared setup/cleanup that needs them.
- No regions, unnecessary priority/owner metadata or order-dependent tests.
- Reproduction tests are wanted when they deterministically exercise the real
  mechanism. Never ship flaky approximations (UI input, timing); diagnose the
  cause first, then fix from it.

## Fixtures, fakes and module boundaries

- Fixtures live in the module's own `tests/TestSupport/` (namespace
  `<Module>.TestSupport`). Never link or copy another module's fixtures. A test
  that exercises another module's logic belongs in that module.
- Other modules are faked through their public ports. A module that others test
  against publishes `projects/<Module>/testing/<Module>.Testing.csproj` with
  fakes of its ports, checked against the real service (see Scripting, Input,
  Textures). Use that kit fake when it exists. Build projects with `TestProject`
  (Projects.Testing) on Testably `MockFileSystem`.
- Moq is allowed for a port that has no kit fake when the test needs a few canned
  answers, a thrown failure or a call check. Mock ports only, never models or the
  code under test. A port mocked repeatedly with the same setups, or needing state
  across calls, gets a kit fake in its owner instead.
- Do not make production types `public` or add `InternalsVisibleTo` so a test
  compiles. If a test needs private state, the seam or the test location is
  wrong; treat it as an ownership question.
- Missing optional inputs (model imports, textures) warn and recover; test the
  warning and the recovery, not a failure.

## Analyzer-clean from the first edit

Write tests that pass the analyzers without a cleanup round:

- Pass `TestContext.CancellationToken` into async calls; use async file APIs in
  async tests.
- Use culture-explicit formatting and parsing; compare strings with an explicit
  comparison, not `ToLowerInvariant`.
- Split long test methods into real private methods (MA0051); local functions
  still count. Follow member order (statics before instance, nested types last)
  and `this.` qualification. Do not suppress diagnostics to move on.

## UI and integration tests

- UI tests use the shared unpackaged host and its dispatcher/content helpers; see
  [UI scaffolding](dn-create-test-project-ui.prompt.md) when creating a host.
- Never drive the system mouse or keyboard, or depend on window focus. Test
  through control APIs, view-model state, raised routed events and bindings.
- Integration tests share one native engine per process (`SharedNativeEngine`
  via `NativeSceneFixture`); restart it only for custom startup settings or
  shutdown tests. No stress loops, and no engine-backed row per field: batch
  shapes into one scene.
- Never import Sponza-class scenes. Small glTF/FBX importer cases are fine.
- GPU-backed tests: run one GPU test process at a time, filtered to the changed
  tests. Never rerun a test that removed the D3D12 device; diagnose it statically
  and hand the rerun to the owner. Do not loop over hanging variants.

## Building and running

- Windows builds use only 64-bit Visual Studio MSBuild
  (`MSBuild\Current\Bin\amd64\MSBuild.exe`) from an x64 developer shell of one
  VS installation. Never use `dotnet build` for projects with C++/CLI references,
  32-bit MSBuild, `Hostx86`, or a bare `MSBuild` without checking its resolved
  path. Run MSBuild from PowerShell, not Git Bash.
- **Build with the Visual Studio edition the owner's build tree uses.** Community
  and Insiders coexist; building with the wrong one invalidates the tree. Detect
  the edition once per task, before the first build, by running this in
  PowerShell from the repository root (it reads the C++/CLI interop compile
  command that the owner's last build left behind):

  ```powershell
  $f = Get-ChildItem artifacts\obj\Oxygen.Editor.Interop\Debug_net10.0\*.command | Select-Object -First 1
  [regex]::Match((Get-Content $f -Raw), 'Microsoft Visual Studio\\(\d+)\\(\w+)').Value
  ```

  The result (for example `Microsoft Visual Studio\18\Insiders`) names the
  install under `C:\Program Files\`. Use its `Common7\Tools` dev shell and
  `MSBuild\Current\Bin\amd64\MSBuild.exe` for every build, and state the edition
  when you build. If the folder or file is missing, stop and ask. Never build with
  another edition, and never repair or reconfigure the tree. The engine tree is
  separate: its edition is the path in `CMAKE_LINKER` in
  `projects/Oxygen.Engine/out/build-ninja/CMakeCache.txt`.

- Make all edits of a pass first, then build once: generate the owning project's
  solution (`projects/<Project>/open.cmd -NoLaunch`, or
  `./tooling/GenerateSolution.ps1 -Scope projects/<Project> -NoLaunch`) and build
  that solution in one parallel call
  (`/restore /m:64 /p:Platform=x64 /p:PreferredToolArchitecture=x64`). Do not build
  projects one by one, hand-write solutions, build while editing, or kill build
  processes by name. Filter build output to `: error `.
- Run only the tests your change touches, never a whole suite:
  `traverse Invoke-Tests --start projects/<module>/tests --configuration Debug -- --filter FullyQualifiedName~MyTest`.
  The runner does not build. A filtered run that failed to start is a failure,
  not a pass.
- Native interop tests use VSTest: pass `"/TestCaseFilter:FullyQualifiedName~X"`
  after `--`. WinUI lanes print nothing through `traverse`; run their `.exe` with
  `--filter ... --report-trx`.
- WinUI lanes (Unit.UI, Integration.UI): at most one targeted pass. On failure or
  a crash, stop, report what is known and hand the owner the exact build and
  `traverse` commands.
- When the owner says they build or run the tests, do not build or run them;
  keep editing and fix what they paste.
- A failing test you hit gets fixed whatever its origin: read the test body (its
  name and assertion are the spec) and the code path in one pass, fix, and rerun
  only that test. Fix warnings in the test files you touched.
- Report failures and unrun checks accurately. See
  [build workflows](../../tooling/doc/build.md).
