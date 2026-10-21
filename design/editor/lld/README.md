# Editor Low-Level Design Index

Status: `active LLD index`

Last Updated: 2026-10-20 01:32

## ED-M08 implementation package

The [ED-M08 plan](../plan/ED-M08-runtime-parity-and-standalone-validation.md)
provides the implementation document map and execution gates. The
[V0.1 capability boundary](scene-authoring-model.md#12-v01-capability-boundary),
[visibility and light participation](scene-authoring-model.md#visibility-participation-and-shadow-source-state)
and
[per-light atmosphere assignment](environment-authoring.md#7-per-light-atmosphere-assignment)
hold the cross-domain decisions and eliminated alternatives. Domain LLDs below
own exact fields, persistence, lifecycle and development qualification details.

Detailed subsystem designs live here. Each LLD must be specific enough to guide
implementation and review, and must trace to [PRD.md](../PRD.md),
[ARCHITECTURE.md](../ARCHITECTURE.md), and [DESIGN.md](../DESIGN.md).

## LLD Set

| LLD                                                                        | Purpose                                                                                                                                     |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| [project-workspace-shell.md](./project-workspace-shell.md)                 | Project Explorer startup, project open/create, workspace activation, restoration, shell/project boundaries.                                 |
| [project-layout-and-templates.md](./project-layout-and-templates.md)       | Standard project filesystem layout, authoring mounts, derived roots, and predefined template output rules.                                  |
| [project-services.md](./project-services.md)                               | Project metadata, project settings, content roots, project cook scope and policy.                                                           |
| [editor-module-architecture.md](./editor-module-architecture.md)           | The editor's modules, layers, naming, ownership and dependency rules after the WorldEditor decomposition.                                   |
| [editor-documents.md](./editor-documents.md)                               | Open documents, their histories, saving, and how the cook meets saved and unsaved edits.                                                    |
| [document-kinds.md](./document-kinds.md)                                   | What every document has in common, the document kinds and how a kind decodes, captures and relocates its content.                           |
| [document-identity.md](./document-identity.md)                             | Document identity, open-once per file, tabs and editor descriptors, document windows and the cooked inspector panel.                        |
| [assets-module.md](./assets-module.md)                                     | `Oxygen.Editor.Assets`: the asset catalog and feed, identity rules, asset rules, the preview port and the publication seam.                 |
| [scenes-module.md](./scenes-module.md)                                     | `Oxygen.Editor.Scenes`: the scene document kind, scene authoring, selection, layout, node lock and visibility.                              |
| [scene-edits.md](./scene-edits.md)                                         | Scene edit records, batches, gestures, history entries, results and the edit catalogue.                                                     |
| [live-preview.md](./live-preview.md)                                       | `Oxygen.Editor.LivePreview`: the preview and play sessions, scene preview, content demand and the engine adapters.                          |
| [documents-and-commands.md](./documents-and-commands.md)                   | Generic document abstractions, document lifecycle, command model, undo/redo, dirty state, selection state, command result flow.             |
| [scene-authoring-model.md](./scene-authoring-model.md)                     | Authoring scene, component completion, commands, dirty state, persistence.                                                                  |
| [scene-explorer.md](./scene-explorer.md)                                   | Hierarchy UI, selection presentation, rename/create/delete/reparent UX, drag/drop semantics.                                                |
| [property-inspector.md](./property-inspector.md)                           | Inspector architecture, component editors, field controls, multi-selection behavior.                                                        |
| [property-pipeline.md](./property-pipeline.md)                             | Canonical schema-property contract, validation, revision/history, mixed values, sessions, and runtime convergence.                          |
| [material-editor.md](./material-editor.md)                                 | Scalar material documents, property editing, assignment, save/cook/preview baseline.                                                        |
| [environment-authoring.md](./environment-authoring.md)                     | Atmosphere, lights, exposure, tone mapping, renderer settings.                                                                              |
| [content-explorer-asset-identity.md](./content-explorer-asset-identity.md) | Content explorer states, asset identity, asset picker, missing/broken references.                                                           |
| [asset-family-modules.md](./asset-family-modules.md)                       | The domain module of each authored asset family (scripts, input, textures, materials): models, rules, problems, facts, ports and templates. |
| [asset-identity.md](./asset-identity.md)                                   | Asset identity, containers and their products, references, producing sources, collisions, and the name and virtual path rules.              |
| [asset-primitives.md](./asset-primitives.md)                               | `Oxygen.Managed.Assets` reusable asset identity, catalog, import/cook primitives, loose index utilities.                                    |
| [content-pipeline.md](./content-pipeline.md)                               | Import, descriptors, manifests, cooking, pak, inspect, mount refresh requests.                                                              |
| [content-cooking-workflows.md](./content-cooking-workflows.md)             | Before/after-cook browsing and picking, shared status, incremental requests, automatic/explicit triggers, progress and recovery.            |
| [cooking-panel.md](./cooking-panel.md)                                     | Dockable cooking progress, session runs, asset-grouped issues, cancellation, save/retry recovery, and property navigation.                  |
| [live-engine-sync.md](./live-engine-sync.md)                               | Managed-to-native live scene synchronization.                                                                                               |
| [runtime-integration.md](./runtime-integration.md)                         | Embedded engine lifecycle, runtime settings, surface leases, views, cooked-root mounts, input bridge, threading/frame phases.               |
| [standalone-runtime-validation.md](./standalone-runtime-validation.md)     | Development-only targets, saved-input preparation, native/embedded capture, private protocol/ABI, comparison and cleanup.                   |
| [viewport-and-tools.md](./viewport-and-tools.md)                           | Viewports, camera navigation, tools, overlays, multi-view.                                                                                  |
| [settings-architecture.md](./settings-architecture.md)                     | Editor, project, workspace, runtime, scene, and diagnostic settings ownership.                                                              |
| [diagnostics-operation-results.md](./diagnostics-operation-results.md)     | Operation results, diagnostics, failure domains, presentation rules.                                                                        |
| [script-authoring.md](./script-authoring.md)                               | Script assets and analysis, node Scripting component and parameters, script cooking, Play and Problems.                                     |
| [input-authoring.md](./input-authoring.md)                                 | Input action and mapping context assets, their editors, key capture, triggers, retargeting and cooking.                                     |
| [workspace-title-bar.md](./workspace-title-bar.md)                         | Workspace title bar: logo, collapsible menu bar of contributed commands, panel reopen, project switcher and Project information.            |
| [texture-editor.md](./texture-editor.md)                                   | Texture Editor: import with detected settings, every cooker setting with its effective value, cooked texels view, cube faces.               |
| [history-panel.md](./history-panel.md)                                     | History panel over the active document's and the project's histories: replay to a step, saved state.                                        |
| [application-density.md](./application-density.md)                         | Application-wide Comfortable/Compact density, the `DroidNet.Density` package, runtime switching and the shared-control contract.            |

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
