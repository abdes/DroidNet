# ED-M07B Main / Inspect / Main native lifetime regression

Validated 2026-09-15 in the packaged Release editor test host, using the installed
Release SDK with D3D12 debug layer and validation explicitly enabled and
conventional directional shadows selected.

`MainInspectionMainActivationKeepsNativeViewsAndSurfacesBalanced` uses the real
EditorDocumentService, DocumentManager, DocumentHostViewModel, SceneEditorView,
Viewport and CookedInspectionView. The scene is saved and cooked through the
content pipeline, synchronized to the native engine, and subjected to the runtime
publication pause/refresh before document activation cycles.

At both 60 FPS and 10 FPS, 30 cycles verify:

- Opening Inspect deactivates the scene view, retires its native view ID and
  returns active surface ownership to zero.
- Selecting Main recreates a loaded scene view with a new native view ID and
  exactly one attached surface; later native-frame observations complete.
- The same two documents remain open; scene metadata stays clean.
- Final view deactivation leaves zero active surfaces and no engine-loop failure.

Result: **2/2 passed**, including 60 total activation cycles with debug validation.
Evidence: `artifacts/TestResults/m07b-document-transition-validated.trx` and
`artifacts/m07b-document-transition-validated-test.log`. The test file has no
unsuppressed analyzer or IDE diagnostics; build output is recorded in
`artifacts/m07b-document-transition-final-build.log`.

This exercises the document-host transition reported with the shadow-resource
state mismatch. The earlier native resource-state-cache regression demonstrated
the faulty lifetime before its fix, with 121 related native cases passing in each
configuration. The existing direct native scene-replacement stress remains
separate coverage. These results do not replace the ED-M10 performance profile.
