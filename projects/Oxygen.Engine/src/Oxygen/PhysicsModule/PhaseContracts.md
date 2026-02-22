# PhysicsModule Phase Contracts (V1)

## Scope

Defines module-level phase behavior for rigid, character, and aggregate-ready domains.

## Phase Semantics

1. `kGameplay`
- Accept scene-authored mutation intents.
- Push kinematic rigid-body transforms.
- Stage command-authoritative aggregate controls (for example vehicles) for fixed simulation.
- Flush deferred structural changes through domain APIs where applicable.

2. `kFixedSimulation`
- Advance backend world simulation only.
- No direct scene graph transform writes.

3. `kSceneMutation`
- Pull physics-authored transforms/states to scene.
- Reconcile deferred node lifecycle mutations.
- Publish physics events mapped to scene handles.

## Authority Matrix

1. Rigid `kStatic`: scene authored placement only; no dynamic pull.
2. Rigid `kKinematic`: scene command authority, pushed in `kGameplay`.
3. Rigid `kDynamic`: physics simulation authority, pulled in `kSceneMutation`.
4. Character: command-authoritative via `ScenePhysics::CharacterFacade::Move`.
5. Aggregate domains:
- Articulation: simulation-authoritative by default.
- Vehicle: command-authoritative control input + simulation integration.
- Soft-body: simulation-authoritative by default.

## Structural Mutation Contract

1. Topology mutations are deferred.
2. Flush points are explicit via `FlushStructuralChanges(...)` API calls.
3. Flush is phase-bounded to avoid unsynchronized backend topology edits.
