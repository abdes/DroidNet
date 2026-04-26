# Oxygen Editor PRD

Status: `active product requirements`

This document defines traceable product requirements for Oxygen Editor V0.1.
Architecture, ownership, and implementation contracts live in
[ARCHITECTURE.md](./ARCHITECTURE.md), [DESIGN.md](./DESIGN.md), and
[lld/](./lld/README.md).

LLDs, work packages, and validation summaries must reference `GOAL-XXX`,
`REQ-XXX`, or `SUCCESS-XXX` IDs from this document. Requirement IDs are
intentionally coarse so implementation can evolve without breaking traceability
on every small workflow step.

## 1. Product Position

Oxygen Editor V0.1 is the first production-quality vertical slice of the
editor. It is not a demo shell and not a temporary proof. Its primary user is
the engine developer who needs to validate that Oxygen scenes can be authored,
previewed through the embedded engine, cooked, and loaded by standalone runtime
code without manual file repair.

Technical artists and tools developers are secondary V0.1 users. Their needs
matter because V0.1 must establish real editor workflows, but breadth is
deliberately constrained to static scene authoring, scalar material authoring,
scoped source import, and live/cooked runtime parity.

## 2. Problem Statement

The editor already has important foundations:

- WinUI shell, routing, docking, tabs, and project/workspace concepts
- Project Browser and project-opening flow
- embedded Oxygen Engine loop and Vortex-rendered viewport
- scene documents, hierarchy editing, and managed scene/domain model
- transform, geometry, camera, and light components
- save, cook, cooked-root mount, and live-engine sync paths

The editor is not yet a dependable authoring product because:

- component and material editors are incomplete or inconsistent
- scene mutations are not uniformly command-based, undoable, or validated
- live sync is direct and imperative rather than a complete component adapter
  model
- content browsing, source import, descriptor generation, cooking, and cooked
  inspection are not one coherent asset workflow
- camera, lighting, environment, exposure, and tone mapping are not presented
  as coherent authoring surfaces
- failures are too often discovered through logs after the fact rather than as
  visible editor operation results

## 3. Goals

| ID | Goal |
| --- | --- |
| `GOAL-001` | Enable an engine-developer validation workflow: author a supported scene, preview it live, cook it, and load the cooked scene in standalone runtime. |
| `GOAL-002` | Deliver usable static scene authoring for procedural meshes, scoped source-imported geometry/material assets, scalar materials, camera, sun, atmosphere, exposure, and tone mapping. |
| `GOAL-003` | Establish embedded live preview parity for authored scene content while allowing editor-only overlays, icons, gizmos, debug visuals, and diagnostics to differ from standalone runtime. |
| `GOAL-004` | Establish a real scalar material editor baseline that can grow into texture and graph workflows later. |
| `GOAL-005` | Make procedural descriptors, scoped source import, generated descriptors/manifests, cooked output, mount state, and content browser selection understandable and actionable. |
| `GOAL-006` | Make failures honest: user actions that fail surface visible operation results and useful logs. |

## 4. Non-Goals

Out of V0.1 scope:

- replacing engine runtime systems with editor-only copies
- editing cooked binary data as source authoring data
- building compatibility bridges to hide bad engine APIs
- full material graph editing
- texture authoring, complex texture graph workflows, and advanced material
  node networks
- physics scene sidecar editing or physics simulation authoring
- animation, prefab, terrain, gameplay, and particle editors
- a full validation dashboard with filters and fix actions
- treating logs as a substitute for operation results where the user initiated
  an editor action

## 5. Requirements

Requirements describe product behavior that must work as specified. The
verification method for that behavior is decided during implementation and
recorded by the owning LLD/work package.

| ID | Requirement |
| --- | --- |
| `REQ-001` | The editor starts at the Project Browser, even when recent project state exists. |
| `REQ-002` | The Project Browser supports recent projects, create/open project, invalid project failure state, and transition into the editor workspace. |
| `REQ-003` | After a project is opened, the editor restores project workspace and recent document/layout state where possible, and makes partial restoration failure visible. |
| `REQ-004` | Scene authoring supports node create, delete, rename, and reparent operations. |
| `REQ-005` | Scene authoring supports add, remove, and edit operations for V0.1 scene components. |
| `REQ-006` | New V0.1 scene-authoring work uses command-based mutation paths that update dirty state. |
| `REQ-007` | Scene data save/reopen round trips supported V0.1 component and environment values. |
| `REQ-008` | Supported scene mutations request live sync when the embedded engine is available. |
| `REQ-009` | V0.1 scene components are Transform, Geometry, PerspectiveCamera, DirectionalLight, Environment, and Material assignment/override. Orthographic cameras, point lights, and spot lights are not V0.1 completion gates unless used by supported workflows. |
| `REQ-010` | Users can create and open scalar material assets through a real material editor. |
| `REQ-011` | Users can inspect and edit scalar material properties through material editor/property UI. |
| `REQ-012` | Users can assign material assets to geometry. |
| `REQ-013` | Users can select material assets from the content browser, with thumbnails or clear visual identity. |
| `REQ-014` | Material values save, reopen, cook, and preview in the embedded engine where supported by engine APIs. |
| `REQ-015` | Content workflow supports procedural geometry descriptors. |
| `REQ-016` | Content workflow supports scoped source import for geometry and scalar material assets. |
| `REQ-017` | The editor generates descriptors/manifests for supported V0.1 scenes and referenced assets. |
| `REQ-018` | The editor cooks the current scene and referenced V0.1 assets into the project cooked output. |
| `REQ-019` | The editor refreshes and mounts cooked output after cooking. |
| `REQ-020` | The content browser shows source, descriptor/generated, and cooked states. |
| `REQ-021` | Asset picking uses asset identity, not raw cooked path text. |
| `REQ-022` | User-triggered import, cook, mount, save, sync, and launch failures produce visible operation results. |
| `REQ-023` | Engine/runtime and pipeline failures produce useful logs. |
| `REQ-024` | Diagnostics identify whether failure is caused by authoring data, missing content, cook output, mount state, sync, or engine runtime state. |
| `REQ-025` | Embedded preview renders visible scene content through Vortex. |
| `REQ-026` | Embedded preview syncs authored V0.1 scene content, materials, and environment where supported by engine APIs. |
| `REQ-027` | The supported V0.1 live viewport layout remains stable and does not abort; multi-viewport layouts are deferred engine/editor work. |
| `REQ-028` | Each supported visible viewport presents to the correct surface/view; V0.1 support is single live viewport unless multi-viewport is explicitly re-scoped. |
| `REQ-029` | Users can navigate the editor camera and frame all/selected. |
| `REQ-030` | Preview parity means the same authored scene content is rendered. Editor overlays, gizmos, selection outlines, node icons, and diagnostics may differ from standalone runtime. |
| `REQ-031` | Late V0.1 viewport UX includes selection highlight. |
| `REQ-032` | Late V0.1 viewport UX includes transform gizmo UX. |
| `REQ-033` | Late V0.1 viewport UX includes icons for non-geometry nodes such as cameras/lights. |
| `REQ-034` | Late V0.1 viewport UX includes useful overlays and debug visual affordances. |
| `REQ-035` | Late V0.1 viewport UX must not block earlier authoring, sync, cook, and runtime parity work. |
| `REQ-036` | The PRD requires round-trip behavior, not one universal persistence schema. Each subsystem LLD decides whether to use engine descriptors directly, augment engine schemas, generate engine descriptors from editor data, or use a hybrid model. |
| `REQ-037` | Supported V0.1 data saves, reopens, cooks, and loads without manual repair. |

## 6. Success Metrics

| ID | Success Metric |
| --- | --- |
| `SUCCESS-001` | The V0.1 workflow completes without manual file edits. |
| `SUCCESS-002` | Supported scene and material edits survive save/reopen. |
| `SUCCESS-003` | Live editor preview shows authored scene content. |
| `SUCCESS-004` | Cooked output loads in standalone runtime with expected geometry, materials, camera, directional light, atmosphere, exposure, and tone mapping. |
| `SUCCESS-005` | The supported V0.1 live viewport does not abort and presents to the correct surface; multi-viewport is deferred. |
| `SUCCESS-006` | Source import, descriptor generation, cook, mount, and standalone load have visible success/failure states. |
| `SUCCESS-007` | Material assets can be created, edited, assigned, cooked, and previewed through real editor UI. |
| `SUCCESS-008` | Viewport selection, node icons, and transform gizmo UX are delivered late in V0.1 rather than blocking early engine/editor plumbing. |
| `SUCCESS-009` | [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md) records one concise validation summary for the milestone. |

## 7. Validation Policy

Validation proves that the requirements work; it is not itself the product
requirement. Each LLD/work package decides the right verification method for its
scope.

Cheap, meaningful automated tests should be added when they give useful signal,
especially for domain logic, serialization round trips, descriptor generation,
asset identity, component defaults, settings behavior, and testable view-models.
Manual workflow validation is acceptable when automation is disproportionately
expensive, especially for WinUI and embedded-engine integration flows. Milestone
validation must reference the relevant `REQ-XXX` and `SUCCESS-XXX` IDs and be
summarized once in [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md).
