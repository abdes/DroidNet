# IndirectLightingService LLD

**Phase:** 7B — Advanced Lighting and GI
**Deliverable:** reserved future LLD
**Status:** `reserved`

## 1. Scope and Context

### 1.1 What This Covers

`IndirectLightingService` — the future stage-13 owner for:

- canonical indirect environment evaluation
- reflections and GI families
- SSAO / ScreenSpaceAO production
- subsurface-adjacent indirect-lighting work

The service is intentionally broad at the ownership level but **not** a
catch-all implementation bucket. Its internal execution order and activation
subset are defined here so the stage can grow in a controlled way instead of
collapsing unlike concerns into one opaque pass.

This document exists now to close the design handoff created in Phase 4. If a
temporary environment-ambient bridge is used before stage 13 exists, that
bridge must retire into this service rather than silently becoming a permanent
stage-12 behavior.

### 1.2 What This Replaces

Any temporary Phase 4 ambient bridge that samples published environment probe
data in stage 12 is explicitly transitional. Once `IndirectLightingService`
activates, stage 12 returns to direct lighting only and stage 13 becomes the
canonical home for indirect environment evaluation.

### 1.3 First Activation Subset

The first activation of stage 13 is intentionally narrower than UE 5.7:

1. **Indirect environment / skylight evaluation** using published
   `EnvironmentFrameBindings`
2. **Ambient-bridge retirement** from stage 12
3. **Optional SSAO production** if it is already available without widening the
   activation too far

SSR, broader reflections, and heavier GI families may follow in later 7B
increments. This downscope is deliberate: it removes the Phase 4 architectural
debt first, then expands stage 13 after ownership is proven in code.

### 1.4 Architectural Authority

- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — future `IndirectLightingService`
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage 13
- [PLAN.md §9](../PLAN.md) — Phase 7B activation and deferred scope

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── IndirectLighting/
        ├── IndirectLightingService.h
        ├── IndirectLightingService.cpp
        ├── Internal/
        │   ├── ReflectionIntegrator.h/.cpp
        │   ├── AmbientOcclusionPipeline.h/.cpp
        │   └── IndirectHistoryStore.h/.cpp
        ├── Passes/
        │   ├── SkyLightingPass.h/.cpp
        │   ├── ReflectionPass.h/.cpp
        │   └── SsaoPass.h/.cpp
        └── Types/
            ├── IndirectLightingFrameBindings.h
            ├── ReflectionHistory.h
            └── ScreenSpaceAoData.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class IndirectLightingService : public ISubsystemService {
 public:
  explicit IndirectLightingService(Renderer& renderer);
  ~IndirectLightingService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 13: indirect lighting / reflections / SSAO / skylight evaluation.
  void Execute(RenderContext& ctx, const SceneTextures& scene_textures);
};

}  // namespace oxygen::vortex
```

### 2.3 Published Payload Contract

```cpp
struct IndirectLightingFrameBindings {
  uint32_t ambient_occlusion_srv{kInvalidIndex};
  uint32_t reflection_result_srv{kInvalidIndex};
  uint32_t diffuse_indirect_srv{kInvalidIndex};
  uint32_t flags{0};
};
```

This payload is intentionally narrower than UE's full view-state history and
denoiser ecosystem. The goal is to publish stable consumer-facing results while
keeping the large family of temporal histories and internal working textures
owned privately by the service.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| SceneTextures | GBufferA–D, SceneDepth, SceneColor | Indirect-light evaluation inputs |
| EnvironmentLightingService | `EnvironmentFrameBindings` | Canonical environment probe / IBL input |
| LightingService | Published direct-light context as needed | Coordination only; not ownership transfer |
| Previous frame | Reflection / AO / temporal histories | Stability and reuse |

### 3.2 Internal Stage-13 Order

```text
IndirectLightingService::Execute(ctx, scene_textures)
  │
  ├─ 0. Resolve activation subset for the current build/config
  │
  ├─ 1. SSAO / ambient-occlusion production (when active)
  │     └─ Produces AO signal for later indirect apply
  │
  ├─ 2. Reflection family (when active)
  │     └─ SSR / reflection-environment / later GI reflection paths
  │     └─ Updates reflection histories
  │
  ├─ 3. Canonical indirect environment / skylight apply
  │     └─ Consumes EnvironmentFrameBindings
  │     └─ Retires any temporary stage-12 ambient bridge
  │
  └─ 4. Subsurface-adjacent indirect work (when active)
```

The order matters. In UE 5.7, indirect environment evaluation, AO, reflections,
and temporal filtering are not one undifferentiated pass. Vortex should keep
that separation even when it groups the family under one service owner.

### 3.3 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Indirect-light contribution | SceneColor | Stage-13 accumulation |
| `ScreenSpaceAO` | SceneTextures or indirect-light bindings | Published downstream product |
| `IndirectLightingFrameBindings` | Later consumers | Published through `ViewFrameBindings` |

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Reflection histories | Persistent per view | Temporal reuse |
| AO history / intermediate buffers | Persistent per view | Optional depending on chosen technique |
| Sky-light / indirect environment history | Persistent per view | Needed once the bridge retires |
| Skylight / indirect PSOs | Persistent | Family-owned |

### 4.1 History Ownership

The service owns a richer history family than the current minimal Phase 4 docs:

- AO history
- reflections / SSR history
- sky-light / indirect environment history
- future GI denoiser histories

This mirrors the important lesson from UE 5.7 without copying its full state
surface: stage 13 cannot be robust if all temporal state is flattened into one
anonymous history blob.

## 5. Stage Integration

- Stage 13 owns canonical indirect environment evaluation.
- Stage 12 must no longer contain a Phase 4 ambient bridge once stage 13 is
  active.
- The service consumes published `EnvironmentFrameBindings`; it does not pull
  environment data through service-internal backdoors.

### 5.1 Explicit Non-Goals For First Activation

The first activation of stage 13 does **not** need to reproduce every UE 5.7
feature at once. The following are intentionally not required in the first
activation unless separately justified:

- full GI / Lumen-equivalent behavior
- all denoiser variants and all temporal sub-histories from day one
- hair-specific indirect-light branches
- niche reflection-capture permutations that add cost without validating the
  main Vortex ownership model

Those items are deferred because they add complexity faster than they increase
architectural confidence.

## 6. Design Decision

The key reason to define this document early is architectural hygiene: Vortex
needs a named future owner for indirect environment lighting so that the Phase 4
bridge cannot fossilize into a permanent stage-order violation. This service is
that owner.

The second key decision is sequencing. Vortex will not activate stage 13 by
trying to swallow all of UE's indirect-light family at once. It will first
activate the subset that removes current architectural debt, then layer in more
complex families behind a service that already owns the stage cleanly.

## 7. Testability Approach

1. Verify stage-12 direct lighting still produces correct output with stage 13
   disabled.
2. Enable stage 13 and confirm the environment-ambient bridge is removed.
3. Verify indirect environment lighting now flows through stage 13 and uses the
   published `EnvironmentFrameBindings`.
4. If SSAO is active, verify it is produced and consumed by stage 13 rather
   than smuggled into unrelated stage families.
5. If reflections are active, verify their histories remain stage-13-owned.

## 8. Open Questions

1. Whether `ScreenSpaceAO` lands in `SceneTextures` immediately or first as an
   indirect-lighting-owned published payload.
2. Which reflection subset should land first after skylight-only activation:
   SSR-first, reflection-capture-first, or another bounded step.
