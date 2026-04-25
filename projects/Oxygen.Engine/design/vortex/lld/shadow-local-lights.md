# ShadowService Local-Light Conventional Expansion LLD

**Phase:** 5G — Remaining Services
**Deliverable:** reserved future LLD
**Status:** `planned`

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

This document captures the planned ShadowService expansion that follows the
Phase 4C directional-first baseline:

- spot-light conventional shadows
- explicit point-light conventional shadow strategy
- per-view publication of local-light shadow products without freezing a VSM ABI

### 1.2 Why This Exists

Phase 4C intentionally narrowed scope to avoid bluffing about point-light
storage. This future LLD is the handoff artifact that closes that gap. The
ShadowService roadmap now has a named later-phase owner for local-light
conventional shadows instead of leaving the issue as an open-ended note.

The corrected Phase 4C contract publishes **directional** conventional shadow
data only. Phase 5G begins from that truthful baseline and extends
`ShadowFrameBindings` without pretending that Phase 4 already shipped
spot-light or point-light conventional shadow payloads.

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — `ShadowService` ownership
- [PLAN.md §7](../PLAN.md) — Phase 5 expansion scope
- [shadow-service.md](shadow-service.md) — Phase 4C directional-first baseline

## 2. Interface Contracts

### 2.1 Service Boundary

No new top-level service is introduced. This is an internal ShadowService
expansion.

### 2.2 Published Payload Contract

`ShadowFrameBindings` remains the canonical GPU-facing publication seam.
Phase 5G may extend it with local-light-specific fields, but it must not bake
today's conventional storage choice into the long-lived binding ABI.

Phase 5G therefore inherits these rules from the remediated Phase 4C baseline:

1. directional conventional shadow publication already exists
2. local-light conventional shadow publication is added here for the first time
3. the public binding seam stays consumer-oriented and does not freeze one
   internal storage layout

### 2.3 Explicit Non-Goals For 5G

The first local-light conventional expansion does **not** include:

- VSM activation or VSM projection logic
- translucent shadow-map targets
- cached preshadow families
- mobile-specific shadow paths

Those remain separate because mixing them into the first local-light expansion
would blur the line between conventional-shadow completion and later
ShadowService upgrades.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | Spot-light data | Spot-light shadow setup |
| Scene | Point-light data | Point-light shadow setup |
| Views | Per-view frusta / relevance | View-scoped publication |
| ShadowService baseline | Directional cascade path | Existing conventional-shadow foundation |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Spot-light shadow publications | LightingService / translucency | `ShadowFrameBindings` through `ViewFrameBindings` |
| Point-light shadow publications | LightingService / translucency | `ShadowFrameBindings` through `ViewFrameBindings` |

## 4. Resource Management

### 4.1 Point-Light Decision Matrix

The design must explicitly choose one point-light conventional storage strategy
before implementation. The baseline options are:

| Option | Pros | Cons | Vortex Verdict |
| ------ | ---- | ---- | -------------- |
| One-pass cubemap depth targets | Closest to UE conventional point-light handling, clean omnidirectional coverage, hardware comparison / filtering story is straightforward | Higher memory footprint than some niche alternatives | **Preferred baseline** |
| Atlased six-face conventional targets | Reuses atlas infrastructure, may simplify allocation bookkeeping | Harder to keep clean face ownership, still effectively a cubemap family with more packing complexity | Acceptable only if it clearly improves Oxygen resource management |
| Dual-paraboloid or other compressed representation | Lower target count in some cases | Projection / filtering complexity, less aligned with UE reference path, higher risk of bespoke artifact handling | Rejected unless a future profiling / platform constraint forces it |

Vortex therefore chooses **one-pass cubemap depth targets** as the default
conventional point-light direction unless later evidence overturns it. This is
the closest useful match to UE 5.7 and avoids inventing a niche representation
with little architectural value.

That choice remains an internal storage decision, not the definition of the
published shadow-binding ABI.

### 4.2 VSM Coexistence Rules

The local-light conventional design must stay compatible with the later VSM
upgrade:

- `ShadowFrameBindings` may expose a technique selector or capability flags,
  but must not hardwire consumers to one conventional storage ABI
- conventional point-light storage and future VSM payloads must coexist behind
  the same published shadow-binding seam
- the 5G choice must not force 7C to keep a cubemap-shaped public contract if
  VSM needs a different internal representation

## 5. Stage Integration

- Work remains under stage 8 and under `ShadowService`.
- Stage 12 and stage 18 continue to consume only published shadow bindings.
- VSM remains a separate later ShadowService upgrade and must not be conflated
  with the conventional local-light expansion.

### 5.1 Chosen First Activation Order

1. Spot-light conventional shadows
2. One-pass cubemap point-light shadows
3. Validation that both flow through the same published binding seam

This order is intentional. Spot lights are cheaper to land and validate first,
but the document now also gives point lights a concrete target rather than
leaving them as an abstract future choice.

## 6. Design Decision

The hard requirement for this future phase is not “implement point-light
shadows somehow.” It is “make the storage choice explicit, bind it through the
existing publication seam, and preserve a clean upgrade path to VSM later.”

The concrete decision taken here is to prefer one-pass cubemap depth targets
for conventional point-light shadows. That aligns with UE's mature path and
gives Vortex a clear conventional baseline before 7C upgrades ShadowService to
VSM.

## 7. Testability Approach

1. Spot-light conventional shadows render correctly in deferred lighting.
2. Point-light conventional shadows render correctly using the one-pass
   cubemap baseline.
3. `ShadowFrameBindings` remains valid for both directional and local-light
   consumers.
4. The same consuming shader path remains compatible with later VSM activation
   through capability / technique selection rather than ABI replacement.

## 8. Open Questions

1. Whether atlased six-face allocation offers any concrete Oxygen-specific win
   over the preferred one-pass cubemap baseline.
2. Whether point-light conventional shadows should remain behind a capability
   gate until content justifies their runtime cost.
