# Live Engine Sync LLD

Status: `reviewed LLD`

## 1. Purpose

Define how authoring scene changes are projected into the embedded Oxygen
Engine.

## 2. Current Baseline

`SceneEngineSync` can create scenes, apply hierarchy/transforms, attach
geometry, cameras, and lights, and call through `OxygenWorld` interop APIs.
This proves the live viewport path but is not yet a complete sync system.

## 3. Target Model

Live sync is a component adapter pipeline:

```text
Authoring change
  -> changed scene/node/component scope
  -> adapter resolves required engine operations
  -> Runtime service checks engine/cooked-root readiness
  -> Interop executes stable engine API
  -> sync state and diagnostics update
```

## 4. Ownership

| Layer | Responsibility |
| --- | --- |
| `Oxygen.Editor.WorldEditor` | Decides what authoring scope changed and requests sync. |
| `Oxygen.Editor.Runtime` | Owns engine lifecycle, readiness, cooked roots, surfaces, and service errors. |
| `Oxygen.Editor.Interop` | Executes stable native engine operations. |
| Oxygen Engine | Owns runtime scene, renderer, asset loading, and diagnostics. |

## 5. Sync Ordering

1. Engine ready and current cooked roots mounted.
2. Scene creation/destruction.
3. Node creation and hierarchy.
4. Transform propagation.
5. Geometry/material components.
6. Lights.
7. Cameras.
8. Environment and post-process.
9. Viewport/view publication.

Asset-dependent components must wait for cooked root readiness or produce a
diagnostic that the component could not sync because content is unavailable.

## 6. Adapter Contract

Each adapter exposes:

- supported component type
- required prerequisites
- create/update/remove operations
- validation before sync
- diagnostics mapping
- idempotency expectations

Adapters should be safe to run repeatedly after scene reload or engine restart.

## 7. Diagnostics

Sync failures include:

- scene ID/name
- node ID/name
- component type
- attempted engine operation
- engine/runtime state
- error message
- whether authoring state remains valid

Sync diagnostics are validation results; logs are supporting evidence.

## 8. Runtime Restarts

If the engine restarts, the sync service must rebuild the live scene from
authoring state:

```text
engine ready
  -> remount cooked roots
  -> recreate scene
  -> replay hierarchy/components/environment
  -> recreate viewport views
```

## 9. Exit Gate

Live sync is complete for a component only when exercised by:

- scene load
- property edit
- undo/redo
- save/reopen
- engine restart/resync
- cook/reload or cooked-root refresh where asset-dependent
