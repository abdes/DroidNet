# Material sidedness and mirrored geometry correction

Status: `validated`

| Field     | Summary                                                              |
| --------- | -------------------------------------------------------------------- |
| Outcome   | Material sidedness and mirrored-geometry rendering corrections.      |
| Remaining | None in the recorded scope.                                          |
| Evidence  | [Record below](#material-sidedness-and-mirrored-geometry-correction) |

Completed 2026-09-15. Engine tests and native-demo validation preceded editor implementation and integrated editor validation.

## Contract and fixes

The glTF 2.0 double-sided and instantiation contracts define single-sided backface
culling, backface-normal reversal for double-sided lighting, and exterior-preserving
winding under a negative global-transform determinant. Before this correction, mesh raster passes ignored the imported material flag.
The current MeshRasterState selects backface culling and world-transform winding
per draw, including translucent pipeline variants.

1. Derive winding reversal from the complete world transform during scene
   collection. Carry it in draw metadata and partition/instancing keys.
2. Select raster state using material sidedness and winding in deferred/forward
   base, depth prepass, auxiliary velocity, and translucency. Double-sided mirrored
   draws also need correct front-face classification for normal reversal.
   Preserve transformed bitangent handedness in the deferred vertex shader, as
   the forward shader already does; otherwise mirrored tangent-space shading
   diverges even when the raster front face is correct.
3. Shadow depth follows material sidedness and world winding. Review light-view
   projection orientation; Oxygen has no separate authored two-sided-shadow flag.
   UE 5.7 `ShadowDepthRendering.cpp::SetupShadowCullMode` follows this material rule,
   with a distinct reversal for its one-pass point projection. Oxygen's existing
   projection path must be checked rather than copying that exception.
4. Keep diagnostic wireframe intentionally uncullled. Fullscreen, sky, grid, and
   light-volume raster policies remain separately owned.
5. Honor the engine's `bake_transforms_into_meshes` import option, including
   inverse-transpose normals, reflected winding, deterministic geometry variants,
   and explicit diagnostics when authored transforms are necessary for fidelity.
   Unbaked meshes retain source-local winding and signed node transforms.
6. After native engine validation, fix editor material-request recovery: absent
   geometry is transient; a loaded geometry with an invalid slot is a real
   rejection. Preserve authored overrides across cook/reload without resurrecting
   explicitly cleared requests.

## Execution and evidence gates

- Implement engine changes and fast tests for actual pass raster bindings,
  adjacent state changes, collection/instancing handedness, and import output.
- Use existing native VortexBasic procedural scene support for a deterministic
  sidedness chart. Validate front/back, single/double-sided, local/inherited
  mirroring, shared geometry, masked/translucent surfaces, cube/sphere controls,
  and both shading paths. Record native screenshots and launch commands.
- Only after that native gate, implement and test editor recovery, rebuild through
  normal MSBuild dependencies, and validate rendering plus cooking/reload recovery
  in the editor. Preserve the repaired Test Cooking project with verified backups.
- Record exact test counts, captures, limitations, and commit the organized
  engine/editor slices. No push.

The original triangle has valid published geometry. A front face without incident
light may remain black. Culling fixes its incorrectly visible back face; it does
not supply illumination. The transient missing material override is a separate
editor defect.

Texture import and conversion of imported native products into editable authored
documents remain separate workflow scope; this correction does not establish
general glTF compliance or complete those workflows.

## Evidence

### Renderer (2026-09-15)

- Debug `oxygen-vortex`, `oxygen-graphics-direct3d12`, and
  `oxygen-examples-vortexbasic` builds passed. ShaderBake compiled 196 modules.
- Four focused existing test executables passed: SceneRendererDeferredCore 56,
  ShadowService 10, DrawMetadataEmitter 14, ScenePrep 40 (120 total).
  The first shadow run exposed a missing fake texture registration; the fixture
  was corrected to match the production allocator and the suite passed.
- Native VortexBasic D3D12 captures, outside the editor:
  `artifacts/sidedness-native-deferred.png`, `sidedness-native-forward.png`,
  `sidedness-native-deferred-normal.png`, `sidedness-native-forward-normal.png`.
  All are under the repository root's artifacts directory.
- In all four lit runs, columns 2/6 are absent across opaque, masked, and
  translucent rows. Single/double-sided fronts, double-sided backs, mirrored
  fronts/backs, inherited mirror and two-reflection cancellation render as
  documented in `Examples/VortexBasic/README.md`. Cube/sphere exterior controls
  are coherent. The normal-map fixture exercises the real texture cooker/loader
  and shows matching mirrored/nonmirrored normal tilt. Deferred shadow silhouettes
  agree with accepted caster faces; the forward path does not display those
  shadows (existing forward-lighting limitation).
- Launch: `Oxygen.Examples.VortexBasic.exe --validation-scene sidedness
--shading-path deferred|forward --fps 30 -v=-1`, optionally
  `--validation-normal-map true`. Captures were made through native CUA and saved
  without editing. Pixel samples are in `artifacts/sidedness-native-pixel-evidence.json`.
- The first lit chart was black because the fixture used an ordinary directional
  light, while current frame-light publication only selects the primary atmosphere
  light slot. The fixture now declares the primary sun, as the default demo does;
  no renderer lighting workaround was introduced. Base-color diagnostic had
  independently confirmed geometry coverage before this fixture correction.
- A bounded native run with `--validation-motion true --debug-layer true
--frames 120` completed with exit 0; log:
  `artifacts/sidedness-native-debug-layer.log`.
- Depth/base/velocity and shadow state transitions are validated at actual Draw
  calls in the fake graphics recorder. Native screenshots establish final rendered
  coverage and shading, not pixel-by-pixel velocity-buffer values.

### Import and editor

- Public-import regression passed: 26 glTF tests (1.879 s) and 2 FBX tests
  (1.428 s), including all 7 new bake tests. These exercise cooked identities,
  geometry bytes, signed transforms, shared/material variants, parent/child
  reflections, retained attachment/animation/morph semantics, and existing full
  scene/light imports. Evidence: `artifacts/sidedness-import-regression-*` logs/XML.
- Editor recovery is implemented and validated: 33 native request tests passed,
  the real editor recovered red material overrides after missing geometry was
  reimported in the same scene session, sidedness/mirror editing and Undo/Redo
  passed, and Save/reopen retained rendering. Detailed evidence is in
  `design/editor/validation/material-sidedness-and-recovery.md` at repository root.
- Prior diagnosis: `artifacts/imported-triangle-rendering-findings.md`.

### Separate existing limitations observed during validation

Current frame-light publication uses one primary atmosphere directional light;
ordinary unassigned directional lights are not published. Forward shading does
not display the deferred chart's shadows. CompositionView depth-prepass settings
are not forwarded by PublishRuntimeCompositionView. These are separate renderer
feature gaps; the validation uses the supported primary-sun/default-depth path.
Scope: material sidedness and mirrored geometry.
