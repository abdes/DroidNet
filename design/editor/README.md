# Oxygen Editor Design Package

Status: `active architecture and roadmap package`

This package is the planning and design home for Oxygen Editor. It is intended
to drive implementation decisions, not merely describe the current state. The
target is a usable world editor that can author, preview, save, cook, reopen,
and run real Oxygen scenes.

## Document Map

| Document | Purpose |
| --- | --- |
| [PRD.md](./PRD.md) | Product requirements, non-goals, and workflow success criteria. |
| [ARCHITECTURE.md](./ARCHITECTURE.md) | Stable system boundaries and ownership contracts. |
| [DESIGN.md](./DESIGN.md) | Cross-cutting design contracts used by all subsystems. |
| [PLAN.md](./PLAN.md) | Milestone roadmap, dependency order, and exit gates. |
| [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) | Authoritative project/file placement rules. |
| [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md) | Current state, milestone validation summaries, and known risks. |
| [RULES.md](./RULES.md) | Non-negotiable engineering rules. |
| [lld/README.md](./lld/README.md) | Low-level design index for active subsystems. |
| [plan/README.md](./plan/README.md) | Detailed work-package plans. |

## Operating Principle

The editor is not a sample UI for the engine. It is the production authoring
environment for Oxygen projects. Its design must keep authoring data, cooked
data, live engine state, project settings, content import, scene editing, and
engine integration as one coherent product.

## Current Planning Focus

The immediate program goal is **Oxygen Editor V0.1**. This is not a prototype
or a temporary proof. It is the first production-quality vertical slice of the
editor and the foundation for later incremental versions.

V0.1 must establish:

1. The editor UI architecture for workspace, docking, scene hierarchy,
   inspector, viewport, content browser, validation, and command surfaces.
2. A complete scene-authoring workflow for the scoped component set: transform,
   geometry, camera, directional light, material assignment, and scene
   environment.
3. Production-ready implementations for the panels and editors in scope. If a
   panel or editor is marked complete, it must have UI, view model, authoring
   model, validation, undo/redo, persistence, live sync, and cook/runtime
   behavior wired end-to-end.
4. A reliable embedded-engine preview path using the Oxygen runtime and Vortex
   renderer through stable runtime/interop contracts.
5. A content and cooking workflow that uses descriptors/manifests and produces
   cooked output usable by standalone engine runtimes.

The V0.1 scene workflow is deliberately scoped, but it must be real:

1. Create a new scene with any number of engine-supported procedurally
   generated meshes, procedural materials without textures, camera,
   directional sun, atmosphere, exposure, and tone mapping.
2. Inspect and edit all V0.1-supported scene data through production editor UI.
3. Preview authored changes live in the embedded engine without restarting.
4. Save, reopen, cook, validate, and mount the scene without manual file repair.
5. Load the cooked scene in a standalone engine runtime and match the live
   editor preview within documented tolerance.

Every milestone and LLD in this package is evaluated against Editor V0.1. Work
outside the V0.1 scope may be deferred, but work inside the scope must be
finished end-to-end.
