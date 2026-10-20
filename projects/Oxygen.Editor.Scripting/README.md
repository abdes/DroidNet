# Oxygen.Editor.Scripting

The script family module: what is true of the editor's scripts whatever shows
them. It holds no view, view model or dialog.

- `Script` is what is known of one script: its identity (the cooked virtual
  path a scene's slot names, such as `/Content/Player/move.oscript`), its
  declared parameters (`ScriptParameter`, `ScriptParameterKind`) and its
  problems (`ScriptProblem`). A script analyzed from its source and one read
  from a cooked library use the same vocabulary; `Origin` says which.
- `ScriptSlotRules` holds the rules of a scene's script slots against their
  scripts, the same for the inspector, the scene edits and the cook: stale
  values, defaults, value and asset types, and a slot's problems
  (`ScriptSlotProblem`). `ScriptFieldAddress` is the `Scripting/{slot}` or
  `Scripting/{slot}/{parameter}` path that leads from a problem to the field.
- `IScripts`, implemented by `ScriptFacts`, holds the open project's scripts as
  the engine's analyzer reads them and their problems. It analyzes a source
  again when its file changes, keeps a broken declaration's last valid
  parameters, and reads library scripts. Its events are raised on each
  subscriber's context.
- Two ports keep the module free of the engine and the pipeline:
  `IScriptAnalyzer` (Live Preview's `NativeScriptAnalyzer`) analyzes a source,
  and `IScriptLibrary` (the content pipeline) reads a script from a mounted
  cooked library.
- `ProjectScriptTooling` keeps a project's VS Code and Luau tooling files
  current from the SDK files `ScriptToolingPaths` finds.
- `ScriptDiagnosticCodes` and `ScriptOperationResults` are the family's codes
  (`OXE.SCRIPT.`) and its Output results.
- `ScriptTemplates.New` is the text of a new script, and
  `ScriptTemplates.Asset` the template New Script creates it from.

A project's files are read and written through the project's file system
(`ProjectContext.FileSystem`); the SDK's definitions and schemas through the
`IFileSystem` the services are given. So the tests run on an in-memory file
system. `Oxygen.Editor.Scripting.Testing` gives other modules' tests
`FakeScripts`, `FakeScriptAnalyzer` and `FakeScriptLibrary`.
