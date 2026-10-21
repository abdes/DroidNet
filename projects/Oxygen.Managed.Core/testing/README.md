# Oxygen.Managed.Core.Testing

The test kit of `Oxygen.Managed.Core`: what other modules' tests use to check
native compatibility without the editor's installed receipt. Only test projects
reference it.

- `TemporaryNativeArtifacts` is an `INativeCompatibilityService` over a private
  receipt that it writes for the given native artifacts, real or synthetic, in a
  temporary folder it deletes on disposal.
- `TemporaryNativeArtifacts.ForInstalledEngine()` writes that receipt for the
  installed engine binaries, import tool, engine content pak, schemas and the
  current Interop copy; a test passes extra artifacts it produces.
