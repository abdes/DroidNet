# Dispatcher-only WinUI tests

Copy this repository template into `projects/<module>/tests`, rename the
`.UI.Tests.csproj` and application namespace, and reference the module under test.
The root configuration supplies the pinned MSTest SDK and common packages.

The shared `UITests.Shared` application initializes WinUI and provides MSTest's
UI dispatcher without opening a window. Use `[UITestMethod]` for dependency
properties and other checks that only need the UI thread. Discovery also remains
window-free. See `SampleTest.cs`.

For layout, focus, templates or loaded/unloaded events, use the same host with
`VisualUserInterfaceTests.EnqueueAsync` and `LoadTestContentAsync`, as shown in
[the realized-control template](../AppSdkWinUITestProject/README.md). There is no
second runner implementation to copy or maintain.

The host is unpackaged both locally and in CI. Tests of package identity require
an explicitly packaged integration host; no MSIX assets are needed here.

From the repository root, after `./init.ps1`, in an x64 VS developer shell.
Use only [64-bit Windows build tools](../../doc/build.md#windows-tool-policy):

```powershell
& "$env:VSINSTALLDIR\MSBuild\Current\Bin\amd64\MSBuild.exe" projects\<module>\tests\<module>.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
traverse Invoke-Tests --start projects/<module>/tests
traverse Invoke-Tests --start projects/<module>/tests -- --filter FullyQualifiedName~MyTest
```

Visual Studio Test Explorer uses the same host and reports the same results.
