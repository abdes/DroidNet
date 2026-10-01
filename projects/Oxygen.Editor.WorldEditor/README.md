# Oxygen.Editor.WorldEditor

WorldEditor coordinates scene authoring and live preview in the Oxygen editor.
Start with [module structure](#module-structure), [development](#development),
and [tests](tests/README.md).

## Module structure

| Area          | Responsibility                                                            |
| ------------- | ------------------------------------------------------------------------- |
| Documents     | Authoring commands, history, selection, persistence and conflict handling |
| Inspector     | Component editors, field diagnostics, geometry and material choices       |
| SceneExplorer | Scene hierarchy and node operations                                       |
| SceneEditor   | Viewport presentation, navigation and camera controls                     |
| Services      | Scene synchronization and content-demand coordination                     |
| Workspace     | Project/session coordination, publication and preview settings            |
| Cooking       | Cooking status, progress, recovery actions and workspace presentation     |
| Inspection    | Read-only inspection of cooked assets                                     |

Content Browser and MaterialEditor are separate modules. Their isolated tests
belong to their own projects; WorldEditor integration tests cover the workflows
that coordinate them with scene documents and native preview.

## Development

Run repository initialization first. The editor consumes an installed Oxygen SDK;
its native CMake build is separate from the Visual Studio project graph.

```powershell
./projects/Oxygen.Editor.WorldEditor/open.cmd
```

The default solution includes all test projects. Use Test Explorer project groups
to select unit, integration or benchmark execution. `-NoLaunch` generates without
opening Visual Studio. Build with Visual Studio/MSBuild and run selected tests in
Test Explorer. See the [repository build guide](../../tooling/doc/build.md).

WorldEditor is a module, not a standalone executable. Launch
`Oxygen.Editor.App` to use it in the editor.
