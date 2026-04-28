# Editor Low-Level Design Index

Status: `active LLD index`

Detailed subsystem designs live here. Each LLD must be specific enough to guide
implementation and review, and must trace to [PRD.md](../PRD.md),
[ARCHITECTURE.md](../ARCHITECTURE.md), and [DESIGN.md](../DESIGN.md).

## LLD Set

| LLD | Purpose |
| --- | --- |
| [project-workspace-shell.md](./project-workspace-shell.md) | Project Browser startup, project open/create, workspace activation, restoration, shell/project boundaries. |
| [project-layout-and-templates.md](./project-layout-and-templates.md) | Standard project filesystem layout, authoring mounts, derived roots, and predefined template output rules. |
| [project-services.md](./project-services.md) | Project metadata, project settings, content roots, project cook scope and policy. |
| [documents-and-commands.md](./documents-and-commands.md) | Generic document abstractions, document lifecycle, command model, undo/redo, dirty state, selection state, command result flow. |
| [scene-authoring-model.md](./scene-authoring-model.md) | Authoring scene, component completion, commands, dirty state, persistence. |
| [scene-explorer.md](./scene-explorer.md) | Hierarchy UI, selection presentation, rename/create/delete/reparent UX, drag/drop semantics. |
| [property-inspector.md](./property-inspector.md) | Inspector architecture, component editors, field controls, multi-selection behavior. |
| [material-editor.md](./material-editor.md) | Scalar material documents, property editing, assignment, save/cook/preview baseline. |
| [environment-authoring.md](./environment-authoring.md) | Atmosphere, lights, exposure, tone mapping, renderer settings. |
| [content-browser-asset-identity.md](./content-browser-asset-identity.md) | Content browser states, asset identity, asset picker, missing/broken references. |
| [asset-primitives.md](./asset-primitives.md) | `Oxygen.Assets` reusable asset identity, catalog, import/cook primitives, loose index utilities. |
| [content-pipeline.md](./content-pipeline.md) | Import, descriptors, manifests, cooking, pak, inspect, mount refresh requests. |
| [live-engine-sync.md](./live-engine-sync.md) | Managed-to-native live scene synchronization. |
| [runtime-integration.md](./runtime-integration.md) | Embedded engine lifecycle, runtime settings, surface leases, views, cooked-root mounts, input bridge, threading/frame phases. |
| [standalone-runtime-validation.md](./standalone-runtime-validation.md) | Cooked-output launch/load validation in standalone runtime and parity evidence. |
| [viewport-and-tools.md](./viewport-and-tools.md) | Viewports, camera navigation, tools, overlays, multi-view. |
| [settings-architecture.md](./settings-architecture.md) | Editor, project, workspace, runtime, scene, and diagnostic settings ownership. |
| [diagnostics-operation-results.md](./diagnostics-operation-results.md) | Operation results, diagnostics, failure domains, presentation rules. |

## Required LLD Sections

Each LLD should use this structure unless it has a documented reason not to:

1. Purpose
2. PRD Traceability
3. Architecture Links
4. Current Baseline
5. Target Design
6. Ownership
7. Data Contracts
8. Commands, Services, Or Adapters
9. UI Surfaces
10. Persistence And Round Trip
11. Live Sync / Cook / Runtime Behavior
12. Operation Results And Diagnostics
13. Dependency Rules
14. Validation Gates
15. Open Issues

## Review Rule

An LLD is not implementation-ready until it states what it owns, what it refuses
to own, which PRD requirements it satisfies, and how the end-to-end workflow is
validated.
