# M07B imported and cooked-only typed assets

Date: 2026-09-15

`ImportedAssetsReachNativeInspectorHistoryAndReopen` passes four packaged
Release cases: glTF and FBX, each used from project import and from an independent
cooked-only library. The final combined native inspector/publication regression
run passes **34/34**.

The source fixtures contain a triangle and scalar material. The glTF fixture
includes its buffer; the FBX fixture adds the same Lambert colour used by the
native numeric-import qualification. Both run through the production native
ImportTool, yielding named scene, geometry and material outputs.

For direct imports, the running workspace publication adapter mounts the new
output. For library cases, a separate producer imports the model; only its
published files are copied into the consumer's library. The producer is disposed
before the consuming catalog, runtime and inspector are opened. The consumer
therefore has no source descriptor or retained import source for those assets.

Each case verifies:

- Shared catalog and picker records show mounted outputs. Project outputs link
  to their retained source and report Current; library-only rows have no source
  or descriptor and cannot be cooked as authored assets.
- Closing either picker without selecting creates no history entry.
- Selecting the material and geometry through real inspector buttons produces
  two undoable commands. Native material colour and geometry buffers update;
  native keys exactly match the indexed catalog keys.
- Undo restores the cube and its original material; Redo restores the selected
  imported assets. Save/reopen preserves both typed URIs and native bindings.
- The real content-demand service creates no extra cook for ready selections.
- Cooking the saved consuming scene succeeds, including when its dependencies
  exist only in the declared cooked library.

The inspector uses the production catalog, picker, demand, document-command and
native services. Existing document-shell and input tests cover the surrounding
navigation and keyboard/pointer behavior; these cases operate the actual
controls through their in-process automation peers.

Evidence: `artifacts/TestResults/m07b-imported-native-use-final.trx`.
Build/analyzer output: `artifacts/m07b-imported-native-use-clean-build.log`.
All changed files have no analyzer or IDE diagnostics.
