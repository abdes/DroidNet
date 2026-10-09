# MSTest migration

Status: C# test projects and templates use MSTest.Sdk 4.5.1 and Microsoft.Testing.Platform
2.5.1 with the shared unpackaged WinUI host. Version adoption is complete in
[DroidNet #19](https://github.com/abdes/DroidNet/issues/19). The DynamicTree UI suite
passed 111 tests in both Debug and Release; packaged-identity paths remain outside
that focused qualification.
See [hosting](#hosting), [release qualification](#release-qualification), and
[WorldEditor test organization](../../projects/Oxygen.Editor.WorldEditor/tests/README.md).

## Hosting

- The SDK is pinned to stable `4.5.1`, with Microsoft.Testing.Platform `2.5.1`.
  Both packages are available from nuget.org; the preview-only upstream feed has
  been removed.
- C# projects use the generated MTP entry point. Native C++ tests retain VSTest.
  Managed test discovery uses `IsTestApplication`; native discovery uses
  `IsTestProject`. Production interop is explicitly not a test project.
- UI programs preserve DroidNet's fixture, dispatcher, realized-content and
  render-wait helpers. Discovery and dispatcher-only cases create no window;
  realized-content cases reuse one window, closed by the host at completion.
- UI hosting is consistently unpackaged locally and in CI. Product application
  packaging remains separate. Tests needing package identity belong in an
  explicitly packaged integration host.
- Project selection uses the Unit, Integration and Benchmarks tiers described in
  [solution generation](solution-files.md). Every UI-hosted test project has `.UI`
  in its name. Full-suite validation is owned by the repository maintainer;
  implementation checks target affected cases.

## Release qualification

The stable pin was built and exercised by the full DynamicTree UI suite in Debug
and Release (111 tests each); the resolved graph includes MTP 2.5.1. This does not
cover Visual Studio Test Explorer discovery/debugging or package identity. Package-
sensitive asset resolution and Project Browser thumbnail loading need a packaged
integration host; unpackaged component tests do not cover those paths.

C# test projects use the supported `EnableMSTestV2CopyResources=false` setting
for English adapter/platform-service diagnostics. This removes their satellite
DLLs from the PRI inputs without suppressing warnings or changing application
localization. Preserve this policy when upgrading the SDK.

Visual Studio may log `Could not determine target device configuration` from
`GetRemoteMachineAddressAsync` even when discovery succeeds. Microsoft identifies
this exact exception as a
[known Test Explorer issue unrelated to MSTest](https://github.com/microsoft/testfx/issues/4729#issuecomment-2613036876).
It does not justify changing local launch profiles or adding remote-device settings.

## Delivery

Keep build infrastructure, runner migration and behavioral/test corrections in
separate commits. The migration branch is `codex/droidnet-build-streamlining`;
its source branch is `codex/ed-m08.1-canonical-data`. After merge approval, use
`git merge --no-ff` to preserve the scoped commit history.
