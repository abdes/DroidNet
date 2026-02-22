# Physics Aggregate Mapping Model (V1)

## Purpose

Define a backend-agnostic mapping model for simulation aggregates where scene-to-physics ownership is not strictly `1 SceneNode : 1 BodyId`.

This model is the contract baseline for articulations, vehicles, ragdolls, and soft-body clusters.

## Core Identity

- `AggregateId`: stable physics aggregate identity in engine-facing APIs.
- `BodyId`: rigid body identity (leaf simulation object).
- `SceneNode` binding remains in `PhysicsModule` and may map to:
  - one aggregate root,
  - one body directly,
  - or both where explicitly documented by domain contracts.

## Mapping Shapes

## 1:N (Aggregate -> Bodies)

Used by articulated systems and vehicles.

- One `AggregateId` owns multiple `BodyId` members.
- Members have aggregate-local semantic roles (for example: root, link, wheel).
- Aggregate lifetime owns member membership contract (attach/detach semantics are domain-driven).

## N:1 (Scene Nodes -> Aggregate)

Used when multiple scene nodes represent one simulated aggregate.

- Multiple `SceneNode` bindings may reference the same `AggregateId`.
- One node is designated aggregate root binding for authority and lifecycle reconciliation.
- Non-root nodes are derived/propagated from aggregate state and domain-specific constraints.

## Authority Contract

- Aggregate authority is explicit and domain-specific:
  - command-authoritative (character/vehicle control path),
  - simulation-authoritative (articulation/soft-body pull path),
  - mixed-authority only when contract explicitly allows it.
- Cross-domain silent fallback is not allowed.

## Lifecycle Contract

- Creation and destruction are aggregate-scoped operations.
- Scene rebinding must preserve stable `AggregateId` where aggregate object identity persists.
- Destroying an aggregate invalidates:
  - aggregate membership map,
  - aggregate-to-node map entries,
  - domain-local state associated with that `AggregateId`.

## Data Model Requirements

- Handle-only at API boundary (`AggregateId`, `BodyId`, `NodeHandle`).
- No raw pointer identity crossing module boundaries.
- Backend internals may use native handles/pointers but must not leak them.

## Phase Integration Requirements

- Structural changes: deferred and phase-bounded.
- Pose push/pull: authority-driven and deterministic.
- Event routing: aggregate-aware mapping before scene-facing publication.

## Non-Goals (V1)

- Full articulation/vehicle/soft-body API implementation.
- Full editor authoring UX.
- Backend-specific optimization details.

## Completion Criteria for Checklist Item 5.0.1

- `AggregateId` exists as a first-class handle in `Handles.h`.
- This document defines the canonical mapping model and authority/lifecycle constraints.
- `to_string(AggregateId)` contract is covered by tests.
