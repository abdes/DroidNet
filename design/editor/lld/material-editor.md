# Material Editor LLD

Status: `scaffold`

## 1. Purpose

Define the V0.1 scalar material editor baseline: material documents, scalar
property editing, preview, asset identity, assignment, save/reopen, cook, and
embedded runtime preview.

## 2. PRD Traceability

- `REQ-010`
- `REQ-011`
- `REQ-012`
- `REQ-013`
- `REQ-014`
- `REQ-021`
- `REQ-022`
- `REQ-037`
- `SUCCESS-002`
- `SUCCESS-004`
- `SUCCESS-007`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 9.3, 10.5, 12, 15
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor.MaterialEditor`,
  `Oxygen.Assets`, `Oxygen.Editor.ContentPipeline`, and
  `Oxygen.Editor.Runtime`

## 4. Current Baseline

To be reviewed against existing material descriptors, cooker support, content
browser selection, geometry material assignment, and live preview support.

## 5. Target Design

Material authoring is a real editor workflow, scoped to scalar properties for
V0.1. It must create/open material assets, edit scalar values, save/reopen,
assign to geometry, cook, and preview through the embedded engine where engine
APIs support it.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.MaterialEditor` | material document UI, scalar property editor, material preview UI |
| `Oxygen.Assets` | reusable material asset identity and primitive asset data |
| `Oxygen.Editor.ContentPipeline` | descriptor/manifest/cook orchestration |
| `Oxygen.Editor.Runtime` | embedded material preview/runtime application |

## 7. Data Contracts

To define:

- material asset identity
- scalar material property model
- material descriptor relationship
- material assignment reference
- missing material state

## 8. Commands, Services, Or Adapters

To define:

- create material asset
- edit material property
- assign material to geometry
- save material
- cook material
- preview material

## 9. UI Surfaces

To define:

- material document
- scalar property inspector
- material asset picker integration
- thumbnail or clear visual identity
- material preview

## 10. Persistence And Round Trip

Scalar material values must save and reopen without manual repair.

## 11. Live Sync / Cook / Runtime Behavior

Material changes must cook and preview where supported by engine APIs. Gaps
must produce diagnostics, not silent dark or fallback rendering.

## 12. Operation Results And Diagnostics

Material create, save, assign, cook, and preview failures must produce visible
operation results.

## 13. Dependency Rules

MaterialEditor must not own reusable cook primitives or call native interop
directly.

## 14. Validation Gates

To define:

- create material
- edit scalar property
- assign to cube
- save/reopen
- cook
- preview embedded
- load standalone

## 15. Open Issues

- Exact V0.1 scalar property set.
- Thumbnail generation source.
