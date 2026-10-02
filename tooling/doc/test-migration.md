# MSTest migration

Status: the C# test projects and templates use MSTest.Sdk and the shared unpackaged
WinUI host. The final-release update remains tracked in
[DroidNet #19](https://github.com/abdes/DroidNet/issues/19).
See [hosting](#hosting), [release qualification](#release-qualification), and
[WorldEditor test organization](../../projects/Oxygen.Editor.WorldEditor/tests/README.md).

## Hosting

- The SDK is pinned to `4.5.0-preview.26480.13`, with MTP
  `2.5.0-preview.26480.13`, from Microsoft's public `test-tools` feed. Its source
  revision is `19db2c848caec237de9ba5e3388e5e694b68ad37`.
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

Before replacing the pin, check Test Explorer discovery, selected execution and
debugging; CLI failure/timeout propagation; resource/native dependency loading;
and host startup/shutdown. Then remove the upstream feed when no longer needed.

Package-sensitive asset resolution and Project Browser thumbnail loading need
explicit coverage. Running unpackaged component tests does not cover those paths.
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
