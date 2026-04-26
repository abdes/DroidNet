# Asset Primitives LLD

Status: `scaffold`

## 1. Purpose

Define the reusable `Oxygen.Assets` primitives used by content browser, content
pipeline, material editor, scene authoring, and project services.

## 2. PRD Traceability

- `REQ-013`
- `REQ-015`
- `REQ-016`
- `REQ-017`
- `REQ-018`
- `REQ-020`
- `REQ-021`
- `REQ-024`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 9.4, 12, 14
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Assets`

## 4. Current Baseline

To be reviewed against current asset reference, catalog, import/cook, loose
cooked index, and validation utilities.

## 5. Target Design

`Oxygen.Assets` owns reusable asset identities, catalogs, import/cook
primitives, loose cooked index utilities, and validation primitives. It does
not own editor workflow or UI.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Assets` | reusable asset/cook primitives |
| `Oxygen.Editor.ContentPipeline` | editor workflow orchestration over those primitives |
| `Oxygen.Editor.ContentBrowser` | user-facing browsing and picking |

## 7. Data Contracts

To define:

- asset identity
- asset reference
- asset catalog record
- import descriptor reference
- cooked virtual path
- loose cooked index record
- asset validation result

## 8. Commands, Services, Or Adapters

To define:

- catalog query primitives
- reference resolution primitives
- import/cook primitive contracts
- loose cooked index validation

## 9. UI Surfaces

None. UI surfaces belong to content browser and feature editors.

## 10. Persistence And Round Trip

Asset references and catalog records must preserve authoring intent across
save/reopen and cook.

## 11. Live Sync / Cook / Runtime Behavior

Asset primitives provide data and validation contracts consumed by content
pipeline and runtime integration. They do not mount cooked roots or call
native interop.

## 12. Operation Results And Diagnostics

Primitive validation produces structured data. User-facing operation results
are emitted by consuming workflows.

## 13. Dependency Rules

`Oxygen.Assets` must not depend on editor UI, runtime services, or native
interop.

## 14. Validation Gates

To define:

- asset identity round-trip
- missing reference classification
- loose cooked index validation
- catalog query behavior

## 15. Open Issues

- Exact URI/identity grammar for V0.1.
- Catalog persistence strategy.
