# ED-M07B native import values and source-copy validation

Validated 2026-09-15 with the existing `Oxygen.Cooker.AsyncImportGltf.Tests`
program and the real asynchronous `AssetLoader`, with content hashes verified.

| Source | Variations | Loaded checks |
| --- | --- | --- |
| glTF | External position buffer; 1x/2x unit multiplier; flat/rotated parent hierarchy | World-space vertex positions and normals, winding, geometry/mesh bounds, retained parent, perspective FOV/aspect/clipping distances and position, material RGBA/metalness/roughness, directional light colour/intensity |
| GLB | Binary position chunk; same scale/hierarchy variations | Same numeric checks through the GLB parser and loaders |
| FBX | Metres/centimetres; both source handedness cases; flat/rotated parent hierarchy | Converted positions/normals/winding/bounds, retained parent, explicit vertical FOV/aspect/clipping distances, Lambert colour and default scalar values, directional light values |

Each of the 16 source profiles is copied, including external files, into a new
source directory with no cooked output. Both copies are imported with the same
options. Their virtual paths, native keys and asset types must match, and both
sets pass the independent expected-value checks: 32 loaded source/output sets.
The FBX fixture explicitly selects vertical aperture mode and adds a scalar
Lambert material; its existing intensity mapping is checked as 100 percent to 1.

Two defects were reproduced and fixed:

- The glTF unit multiplier changed geometry and camera position but left clipping
  planes unchanged. Camera linear distances now use the same multiplier.
- Both adapters applied pruning reparent rules to retained parents. A rotated
  parent could become disconnected even with `kKeepAll`. Retained parents now
  require only index remapping; their child transforms and hierarchy remain intact.

Evidence:

- Camera-unit failing baseline: `artifacts/m07b-loaded-values-Debug-baseline.log`.
- Hierarchy failing baseline: `artifacts/m07b-loaded-values-clean-copy-tests.log`.
- Related import/source-inspection suites: 23/23 in Debug and 23/23 in Release,
  including existing glTF/FBX and Sponza cases:
  `artifacts/m07b-import-numeric-{Debug,Release}-tests.log`.
- Final expanded numeric cases: 2/2 in each configuration, covering the 16 profiles:
  `artifacts/m07b-import-portable-{Debug,Release}-tests.log`.
- Build logs: `artifacts/m07b-import-portable-{Debug,Release}-build.log`;
  no compiler warnings/errors. Both SDK configurations installed.

The tests validate CPU asset values. Rendered equivalence remains ED-M08's gate.
