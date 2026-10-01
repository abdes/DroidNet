# Realized WinUI control tests

Copy this repository template into `projects/<module>/tests`, rename the
`.UI.Tests.csproj` and application namespace, and reference the module under test.
The root configuration supplies the pinned MSTest SDK and common packages.

The application inherits `UITests.Shared`'s host. Derive control tests from
`VisualUserInterfaceTests`, dispatch with `EnqueueAsync`, and await
`LoadTestContentAsync` before asserting layout, focus or visual state. A single
800×600 window is created on demand and reused; fixture cleanup unloads its
content. Discovery creates no visible window. See `SampleTest.cs`.

The host is unpackaged both locally and in CI. It needs no MSIX manifest or
package logos. Keep actual test images and resources, and deploy them explicitly.
Tests of package identity belong in a deliberately packaged integration host.

From the repository root, after `./init.ps1`:

```powershell
MSBuild.exe projects/<module>/tests/<module>.UI.Tests.csproj /restore /m /p:Configuration=Debug
traverse Invoke-Tests --start projects/<module>/tests
traverse Invoke-Tests --start projects/<module>/tests -- --filter FullyQualifiedName~MyControl
```

Visual Studio Test Explorer uses the same host. The test process closes its
window and returns the runner's exit code on completion. For suite-wide migration
status, see [the migration plan](../../doc/test-migration.md).
