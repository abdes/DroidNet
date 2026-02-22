# Physics Domain Separation Contract (V1)

## Intent

Preserve strict backend-agnostic separation between scene integration and physics domain APIs as new aggregate domains are introduced.

## Boundary Rules

1. Domain interfaces in `src/Oxygen/Physics/System` are handle-based only.
2. Scene types (`SceneNode`, `NodeHandle`, scene components) must not appear in domain API signatures.
3. Backend native handles/pointers must not cross the `Oxygen.Physics` public API boundary.
4. Scene/physics reconciliation remains the responsibility of `PhysicsModule`, not backend domain APIs.

## Mapping Rules

1. Scene-facing mapping tables live in `PhysicsModule`.
2. Physics-facing identity lives in typed handles (`WorldId`, `BodyId`, `AggregateId`, etc.).
3. Aggregate domains may express 1:N and N:1 relationships using handle maps only.

## Evolution Rules

1. New domains must be added as dedicated interfaces (`I*Api`) and optional accessors on `IPhysicsSystem`.
2. No cross-domain "god interface" methods.
3. Structural mutation points must be explicit (`FlushStructuralChanges` where relevant).

## Verification

1. API contract tests in `Physics_test.cpp` validate stable accessor behavior and extension availability patterns.
2. Backend integration tests validate behavior, but not boundary ownership (owned by this contract).
