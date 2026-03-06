# PhysicsModule Phase Contracts (V1)

## Scope

Defines module-level phase behavior for rigid, character, and aggregate-ready domains.

## Phase Semantics

1. `kGameplay`
- Accept scene-authored mutation intents.
- Push kinematic rigid-body transforms.
- Stage command-authoritative aggregate controls (for example vehicles) for the next `kFixedSimulation` step.
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
4. Character: command-authoritative via `ScenePhysics::MoveCharacter`.
5. Aggregate domains:
- Articulation: simulation-authoritative by default.
- Vehicle: command-authoritative control input + simulation integration.
- Soft-body: simulation-authoritative by default.

## Structural Mutation Contract

1. Topology mutations are deferred.
2. Flush points are explicit via `FlushStructuralChanges(...)` API calls.
3. Flush is phase-bounded to avoid unsynchronized backend topology edits.

## Aggregate Control Latching Contract (Required)

1. `kFixedSimulation` executes before `kGameplay` in each frame.
2. Aggregate control inputs are sampled by physics integration at `kFixedSimulation`.
3. Writes completed before `kFixedSimulation` are consumed in the current frame.
4. Writes in/after `kGameplay` are latched for the next `kFixedSimulation` step.
5. For multiple writes to the same aggregate within one frame window, last-write-wins deterministically.

## Transform Readiness Contract (Required)

1. Physics API boundary pose fields (`initial_position`, `initial_rotation`) are world-space.
2. `kSceneMutation` runs before `kTransformPropagation`; world transform caches are not guaranteed ready there.
3. Default pre-propagation rule: scene-side hydration code must not depend on live `GetWorld*` reads unless a hydration transform barrier has been established.
4. Hydration-only exception: scene hydration coordinator may call `ResolveHydrationTransforms` (internally one `Scene::Update()`) to establish world-transform readiness before seeding physics descriptors; repeated calls in the same hydration window are idempotent no-ops.
5. After the hydration barrier, world reads are authoritative for hydration seeding; post-propagation runtime world reads remain valid as usual.
6. Add phase-aware guardrails so pre-propagation world reads fail fast when no hydration readiness token is active, and invalidate token use after hydration exits.
7. No mixed fallback policy (`world if available else local`) is allowed for hydration. This hides phase-order bugs and causes nondeterminism.
