# Material sidedness and publication recovery correction

Status: validated (2026-09-15). This is a focused correction to the existing
rendering/publication workflow. Earlier milestone evidence retains its scope.

## Behavior

- The engine culls single-sided backfaces, renders double-sided backfaces with
  reversed normals, and preserves the exterior under negative world scales.
- glTF/FBX static transform baking honors the engine option; the editor's
  static-scalar import policy continues to retain transforms.
- The editor retains the current authored material assignment while its geometry
  is missing or fails to load. An older visible geometry cannot validate the
  replacement's material-slot layout. Once the current geometry loads, a truly
  invalid slot is rejected. Removed slots, explicit None, and newer assignments
  keep their existing precedence.

## Engine gate completed before editor implementation

Engine commits: `b78d07421` (renderer) and `26f5cf1e4` (importer).
120 renderer tests, 26 glTF and 2 FBX tests passed. Native VortexBasic scalar and
normal-map charts passed in deferred and forward rendering, outside the editor.
The shader library compiled 196 modules. A bounded native run with animation and
the D3D12 debug layer completed with exit 0.

Detailed engine scope and limits:
[material-sidedness-correction.md](../../../projects/Oxygen.Engine/design/vortex/plan/material-sidedness-correction.md).

## Editor implementation and automated evidence

`SceneAssetRequests` distinguishes a failed current geometry request from an
available current geometry. Material application waits through the former, while
the existing out-of-range-slot rejection remains active for the latter.

Debug/x64 MSBuild passed. All 33 `InteropTests.AssetRequestTests` passed in 0.5383 s,
including five new synchronous recovery/order/precedence cases. Logs and TRX:

- `artifacts/editor-material-recovery-build.log`
- `artifacts/editor-material-recovery-tests.log`
- `artifacts/TestResults/editor-material-recovery.trx`

## Live editor validation

Passed. Use only the isolated **Sidedness Validation** project at
`artifacts/editor-sidedness-validation/Project/Project.oxy`.
The complete baseline and SHA256 manifests are beside it; preparation verified
all 76 original Test Cooking files unchanged and all 51 derived files preserved.
The fixture changes only Project.oxy, Main, and a new double-sided red material.

Observed results:

1. With triangle geometry absent and both red materials available, the running
   editor displayed the red cube/sphere controls and retained `M_ShinyRed` in the
   triangle inspector. **Reimport source** published geometry in operation
   `7ef3006e-f462-4ec8-ba15-20eea0c5c265` (`WasMounted=true`). In the same scene
   session, five expected triangles appeared red; the single-sided back remained
   absent. There was no scene/project reload to recover these overrides.
2. Local scale X changed from -1.4 to +1.4 while retaining a lit exterior. Undo,
   Redo, Undo restored each expected silhouette/value, then Main was saved.
3. In Material Editor, Double Sided changed from true to false and was saved.
   The double-sided-back case disappeared; its front remained visible. Material
   Undo and Save restored true and the lit backface returned. Native material
   publications mounted successfully.
4. Main **Cook Selected Asset** completed with only the fixture's existing
   missing-indices and missing-tangent-prerequisites warnings. Main and imported
   assets were Ready. The current Main matched its earlier committed publication,
   so reuse was valid. The final receipt is
   `f89c1793-dd39-48cf-b980-8f8b2fc7b99b`: 7 products, 9 outputs, 14 published files,
   all matching receipt/provenance/journal, phase Committed, `WasMounted=true`.
5. The editor closed cleanly and reopened Main. All five expected triangles,
   cube and sphere rendered red, the single-sided back remained absent, and the
   selected mirrored node still showed scale X=-1.4 and `M_ShinyRed`. No startup
   cook was needed. The editor is left open on this validation scene.
6. Original Test Cooking and its complete baseline each still match all 76
   original hashes. The fixture's 26 authored files differ from the baseline only
   in the three intended paths. Retained glTF/settings are unchanged.

Evidence under `artifacts/editor-sidedness-validation/`:

- `missing-geometry.png` and `recovered-after-source-cook.png`
- `positive-scale-edit.png`, `negative-scale-undo.png`, `scale-redo-ui.txt`
- `double-sided-off.png` and `double-sided-restored.png`
- `final-main-cook.png`, `reopened-final.png`, `reopened-final-ui.txt`
- `final-integrity.json` and `loaded-native-modules.json`

## Build and analysis integrity

The final App dependency build passed using MSBuild Debug/x64. Its embedded
receipt matches all 30 installed native runtime DLLs. An earlier launch correctly
rejected Core/Cooker files changed by a concurrent native build/install during
Interop compilation; rebuilding against the stable SDK resolved it without
bypassing validation. The current native binaries then passed all 148 engine
regressions again. Evidence: `artifacts/sidedness-stable-sdk-receipt.json`,
`sidedness-editor-app-stable-sdk-build.log`, and `sidedness-stable-sdk-*` test XML.

Selected-file native analysis produced zero diagnostics in the changed source
and tests. Fresh SARIF files and the generic VC task-warning explanation are in
`artifacts/editor-material-recovery-analysis/summary.json`. Only an external fmt
header warning remained. All 3,067 protected SDK/deployed files stayed unchanged
through analysis; no linking or SDK installation occurred.
