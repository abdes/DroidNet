# Runtime Integration LLD

Status: `scaffold`

## 1. Purpose

Define the managed runtime boundary for embedded Oxygen Engine lifecycle,
runtime settings, surface leases, views, cooked-root mounts, runtime
diagnostics, and UI-thread/engine-frame coordination.

## 2. PRD Traceability

- `REQ-019`
- `REQ-022`
- `REQ-023`
- `REQ-024`
- `REQ-025`
- `REQ-027`
- `REQ-028`
- `REQ-035`
- `SUCCESS-003`
- `SUCCESS-005`
- `SUCCESS-006`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 8.5, 11, 12.4, 14, 15
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor.Runtime` and
  `Oxygen.Editor.Interop`

## 4. Current Baseline

To be reviewed against current engine service, runtime settings application,
surface leasing, view creation, cooked-root mount, and native interop usage.

## 5. Target Design

The runtime integration layer is the managed boundary for engine lifecycle and
live preview. Feature UI requests runtime capabilities; it does not call native
interop directly.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.Runtime` | lifecycle, settings, surfaces, views, mounts, runtime diagnostics |
| `Oxygen.Editor.Interop` | managed/native bridge and native engine capability execution |
| Oxygen Engine | frame loop, rendering, content loading, graphics resources |

## 7. Data Contracts

To define:

- runtime state
- runtime settings snapshot
- surface lease
- view request/result
- mount request/result
- frame-phase completion semantics

## 8. Commands, Services, Or Adapters

To define:

- start/stop runtime
- apply settings
- attach/detach surface
- create/destroy view
- resize surface
- mount/unmount cooked root
- runtime diagnostics adapter

## 9. UI Surfaces

To define:

- runtime status presentation
- surface/view failure presentation
- runtime diagnostic output

## 10. Persistence And Round Trip

Runtime state is not source authoring data. Runtime settings derive from
editor/project settings and must be reproducible after restart.

## 11. Live Sync / Cook / Runtime Behavior

Runtime integration owns the safe marshal point between UI, async managed
services, and native engine frame phases.

## 12. Operation Results And Diagnostics

Lifecycle, surface, view, resize, mount, settings, and runtime failures must
produce operation results when triggered by user workflow.

## 13. Dependency Rules

Runtime may depend on Interop. Feature UI depends on Runtime. Interop must not
own editor authoring or project policy.

## 14. Validation Gates

To define:

- runtime starts
- cooked root mounts
- one/two/four surfaces present correctly
- resize is stable
- engine restart resyncs views
- runtime failure is visible

## 15. Open Issues

- Precise async completion semantics for accepted, frame-committed, and
  presented operations.
