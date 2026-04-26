# Content Browser And Asset Identity LLD

Status: `scaffold`

## 1. Purpose

Define the content browser UX, asset identity model, asset picker behavior, and
source/generated/cooked/missing asset states.

## 2. PRD Traceability

- `REQ-013`
- `REQ-020`
- `REQ-021`
- `REQ-022`
- `REQ-024`
- `SUCCESS-006`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 10.6, 12.1, 14, 15
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor.ContentBrowser`,
  `Oxygen.Assets`, `Oxygen.Editor.ContentPipeline`, and
  `Oxygen.Editor.Projects`

## 4. Current Baseline

To be reviewed against current content browser routing, folder views, asset
catalogs, cooked output display, and selection state.

## 5. Target Design

The content browser shows project source, generated descriptor, cooked, mounted,
missing, and broken states. Asset pickers return asset identity rather than raw
cooked filesystem paths.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentBrowser` | navigation, display, selection, asset picker UX |
| `Oxygen.Assets` | asset identity, catalog data, reference primitives |
| `Oxygen.Editor.ContentPipeline` | source/generated/cooked state production and pipeline operations |
| `Oxygen.Editor.Projects` | content roots and project policy |

## 7. Data Contracts

To define:

- asset identity
- asset reference
- asset catalog record
- source/generated/cooked state
- missing/broken reference state
- picker result

## 8. Commands, Services, Or Adapters

To define:

- catalog refresh
- asset picker query
- asset resolve
- missing reference resolution
- pipeline action invocation

## 9. UI Surfaces

To define:

- source tree
- descriptor/generated view
- cooked output view
- asset type views
- asset picker dialog/panel
- missing/broken diagnostic presentation

## 10. Persistence And Round Trip

Asset selections must persist as authoring identities that can be resolved
again after reopen.

## 11. Live Sync / Cook / Runtime Behavior

The content browser does not mount cooked roots or mutate scenes directly. It
invokes content-pipeline/runtime services through public contracts.

## 12. Operation Results And Diagnostics

Catalog refresh, picker resolution, and pipeline action failures must be
visible.

## 13. Dependency Rules

ContentBrowser must not depend on WorldEditor internals or own scene mutation
policy.

## 14. Validation Gates

To define:

- source asset visible
- generated descriptor visible
- cooked asset visible
- missing reference shown
- material picker returns asset identity

## 15. Open Issues

- Exact catalog indexing policy.
- Thumbnail/preview generation source.
