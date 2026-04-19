# Depth Prepass Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.4
**Status:** `ready`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## 1. Scope and Context

### 1.1 What This Covers

`DepthPrepassModule` is the stage-3 owner responsible for:

- depth-only rendering for opaque and masked deferred participants
- `SceneDepth` population
- `PartialDepth` publication
- truthful `DepthPrePassCompleteness`

Under the active desktop deferred opaque-velocity policy, stage 3 does **not**
publish opaque `SceneVelocity`.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 2 (InitViews) | Current-view prepared-scene payload published |
| **This** | **Stage 3 — DepthPrepass** | Depth-only |
| Successor | Stage 5 (Occlusion/HZB — reserved) or Stage 9 (BasePass) | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — UE family mapping
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — runtime stage table
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [PLAN.md §5](../PLAN.md) — Phase 3 work items
- UE5 reference: `RenderPrePass`, `DDM_AllOpaque*` policy family

### 1.4 Required Invariants

- `SceneRenderer` owns per-view iteration; `DepthPrepassModule::Execute(...)`
  consumes the current view only
- masked opaque participation here is depth-policy work only; masked materials
  still belong to the deferred opaque/base-pass contract
- stage 3 applies an explicit stage-local front-to-back refinement within the
  accepted opaque and masked depth buckets using the current view plus the
  published per-draw bounds; it must not rely on accidental upstream ordering
- under the active desktop deferred opaque-velocity policy, stage 3 must not
  publish opaque `SceneVelocity`
- any future depth-pass velocity path must be enabled only through an explicit
  policy change, not by reusing dormant booleans

## 2. Interface Contract

### 2.1 File Placement

Per [PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md):

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── DepthPrepass/
            ├── DepthPrepassModule.h
            ├── DepthPrepassModule.cpp
            ├── DepthPrepassMeshProcessor.h
            └── DepthPrepassMeshProcessor.cpp
```

### 2.2 Public API Shape

```cpp
namespace oxygen::vortex {

struct DepthPrepassConfig {
  DepthPrePassMode mode{DepthPrePassMode::kOpaqueAndMasked};
};

class DepthPrepassModule {
 public:
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);
  void SetConfig(const DepthPrepassConfig& config);
  [[nodiscard]] auto GetCompleteness() const -> DepthPrePassCompleteness;
};

}  // namespace oxygen::vortex
```

`write_velocity` is not part of the active stage-3 contract under the current
desktop deferred opaque-velocity policy.

## 3. Inputs and Outputs

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| InitViewsModule (stage 2) | Current-view `PreparedSceneFrame` payload | Determines what to draw without scene re-traversal |
| SceneTextures | SceneDepth DSV | Depth target |
| Renderer | depth-only PSO cache | Pipeline state |
| Geometry / deformation inputs | Current-frame draw inputs only | Geometry/depth evaluation |

### 3.2 Outputs

| Product | Format | Written To |
| ------- | ------ | ---------- |
| SceneDepth | D32_FLOAT_S8X24_UINT | SceneTextures::GetSceneDepth() |
| PartialDepth | copy of SceneDepth | SceneTextures::GetPartialDepth() |

### 3.3 Setup Milestones

```text
Before stage 3:
  SceneDepth   = allocated, cleared
  PartialDepth = allocated, undefined
  Velocity     = allocated but not owned by stage 3 under current policy

After stage 3:
  SceneDepth   = written (opaque + masked depth)
  PartialDepth = copy of SceneDepth
  Velocity     = unchanged / unavailable for opaque use under current policy
```

## 4. Execution Contract

```text
DepthPrepassModule::Execute(ctx, scene_textures)
  ├─ if mode == kDisabled -> return
  ├─ read current-view PreparedSceneFrame
  ├─ build stage-local depth draw commands
  │    ├─ partition accepted draws into opaque vs masked buckets
  │    ├─ stable-sort each bucket front-to-back using the published draw bounds
  │    └─ fall back to accepted-draw order only if the current view or bounds are unavailable
  ├─ bind depth-only framebuffer
  ├─ issue opaque depth draws
  ├─ issue masked depth draws with alpha-test permutation
  ├─ copy SceneDepth -> PartialDepth
  └─ publish completeness state
```

Stage 3 must not synthesize an opaque velocity publication seam while the
active opaque-velocity policy is `kBasePass`.

## 5. Future Policy Boundary

UE 5.7 supports an explicit depth-pass velocity policy. Vortex keeps that as a
future architectural option.

If Vortex ever activates depth-pass opaque velocity, the following must happen
together:

1. the renderer-level opaque velocity policy changes explicitly
2. stage-3 binds and writes a real velocity target
3. stage-9 stops claiming ownership for the same producer scope
4. docs/tests/validation are updated as one policy change

This LLD does not authorize mixing both policies implicitly.

## 6. Testability and Validation

1. **Unit test:** depth-prepass draw command count and masked participation
   follow the prepared-scene payload.
2. **Ordering test:** opaque and masked depth buckets are refined front-to-back
   using the documented stage-local key, and equal-key cases preserve their
   original relative order.
3. **Integration test:** SceneDepth and PartialDepth become valid after stage 3.
4. **Policy test:** stage 3 never marks or publishes opaque `SceneVelocity`
   under the active desktop deferred base-pass velocity policy.

## 7. Closure

Any future depth-pass opaque velocity path must be introduced as an explicit
policy-level design change, not as a local module tweak.
