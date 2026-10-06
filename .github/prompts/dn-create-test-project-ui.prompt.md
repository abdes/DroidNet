---
mode: "agent"
description: "Scaffold a WinUI test project using the shared unpackaged MTP host"
---

# DroidNet UI test project

Follow root/module `AGENTS.md` and
[MSTest conventions](csharp-mstest.prompt.md). Read these templates before creating
files; they, not a copied packaged-app checklist, are the source of truth:

- [Realized UI tests](../../tooling/samples/AppSdkWinUITestProject/README.md) for
  layout, focus, visual-state and control interaction tests.
- [Window-free AppSDK tests](../../tooling/samples/AppSdkNoWindowTestProject/README.md)
  for dispatcher/AppSDK behavior that does not require realized content.

## Scaffold

- Place the project in the owning module's test hierarchy and name it
  `<module>.UI.Tests.csproj`. Copy the relevant template, rename its namespaces,
  and reference the module under test. Preserve `MSTest.Sdk` and the
  `UITests.Shared.projitems` import; root configuration supplies common packages
  and host settings. Retain required template settings such as unsafe compilation;
  add Moq only when the tests need it.
- The shared host is unpackaged. Do not add `Package.appxmanifest`, package logos,
  signing/publishing scaffolding or packaged launch profiles unless package
  identity is explicitly required. Keep and deploy actual test resources.
- Use the template's application class and test base. For realized content,
  dispatch with `EnqueueAsync` and await `LoadTestContentAsync`; use existing
  render/event helpers rather than sleeps. Preserve its nonparallel execution
  and fixture cleanup; do not copy blanket analyzer suppressions.
- Reuse the shared window lifecycle: it is created on demand, content is unloaded
  between fixtures, and the host exits when tests finish. Do not create a separate
  application/window per case or assume discovery displays a window.

## Verify

From the repository root in an initialized x64 VS developer PowerShell.
Only 64-bit MSBuild and x64-hosted compilers are allowed:

```powershell
& "$env:VSINSTALLDIR\MSBuild\Current\Bin\amd64\MSBuild.exe" projects\<module>\tests\<module>.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
traverse Invoke-Tests --start projects/<module>/tests --configuration Debug -- --filter FullyQualifiedName~MyControl
```

Substitute the actual path and test name. Check discovery and the intended UI
behavior; a successful build alone is not test evidence. Report unrun checks.
