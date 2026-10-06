---
agent: "agent"
description: "Write focused MSTest tests using DroidNet tooling and fixtures"
---

# DroidNet MSTest tests

- Follow root/module `AGENTS.md` and the owning suite. Managed tests use
  `MSTest.Sdk`, pinned by `global.json`, not hand-added MSTest runner packages or
  legacy Coverlet flags. Project names end in `.Tests` (`.UI.Tests` for WinUI).
- Reuse `projects/TestHelpers` and the relevant templates under `tooling/samples`.
  Put versions in `Directory.packages.props`; add Moq only when needed. Do not
  introduce another mocking library or a DI container just for a test.
- Use `[TestClass]`, `[TestMethod]` and scenario-focused names such as
  `Method_Scenario_ExpectedBehavior`. Use `[DataRow]`/`[DynamicData]` for meaningful
  variations; use lifecycle attributes only for shared setup/cleanup that needs
  them. No regions, unnecessary priority/owner metadata or order-dependent tests.
- Assert observable behavior with the suite's AwesomeAssertions/MSTest patterns.
  Add messages only for missing context; avoid mock/private-call assertions that
  lock tests to an implementation rather than its contract.
- UI tests use the shared unpackaged host and its dispatcher/content helpers.
  See [UI scaffolding](dn-create-test-project-ui.prompt.md) when creating a host.
- Windows builds use only 64-bit MSBuild (`MSBuild\Current\Bin\amd64\MSBuild.exe`)
  and x64-hosted compilers, from the same VS installation. Never use bare
  `MSBuild` without checking its resolved path, 32-bit MSBuild, or `Hostx86`.
- Build the owning project with 64-bit Visual Studio MSBuild
  `/restore /m /p:Platform=x64 /p:PreferredToolArchitecture=x64`, then:
  `traverse Invoke-Tests --start projects/<module>/tests --configuration Debug -- --filter FullyQualifiedName~MyTest`.
  Substitute the actual module/test name. The runner does not build; report
  failures and unrun checks accurately. See [build workflows](../../tooling/doc/build.md).
