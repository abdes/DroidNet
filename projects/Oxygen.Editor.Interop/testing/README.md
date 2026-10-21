# Oxygen.Editor.Interop.Testing

The test kit of `Oxygen.Editor.Interop`: what test projects that load the
interop assembly use to find the installed engine SDK. Only test projects
reference it.

- `NativeDllSearchPath.Configure()` registers the engine SDK's `bin` folder,
  `projects/Oxygen.Engine/out/install/{Configuration}/bin`, with the Windows DLL
  loader, so the tests run against the installed engine instead of copies in
  their output. A test project calls it from its own module initializer.
- `ResolveEngineInstallDirectory()` is that SDK's root.
