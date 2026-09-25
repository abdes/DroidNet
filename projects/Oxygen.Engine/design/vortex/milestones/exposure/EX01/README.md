# EX01 — Exposure contracts

Status: `validated`

| Field     | Summary                                                     |
| --------- | ----------------------------------------------------------- |
| Outcome   | Exposure equations, units, layouts and numerical contracts. |
| Remaining | None in the recorded scope.                                 |
| Evidence  | [Validation record](validation.md)                          |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

- Update [PBR specification](../../../../renderer-core/physically-based-rendering.md)
  with equations, units, examples, numeric domain and the complete target behavior.
- Reconcile [physical-lighting roadmap](../../../../renderer-core/physical-lighting-roadmap.md),
  [panel design](../../../../renderer-core/post-process-panel-design.md), and repository-root
  [environment authoring](../../../../../../../design/editor/lld/environment-authoring.md).
- Expand [PostProcessService LLD](../../../lld/post-process-service.md) with the state
  layout, hybrid solve, lifecycle policies, masks/curve, numerical bootstrap and
  failure behavior in this plan. Include state and frame-sequence diagrams.
- Update [multiview](../../../lld/multi-view-composition.md),
  [InitViews](../../../lld/init-views.md), [shader contracts](../../../lld/shader-contracts.md),
  [scene textures](../../../lld/scene-textures.md), and environment/lighting LLDs for
  frame-pinned P, sharing and bootstrap format support.
- Fix the supported radiance envelope, operational FP32 bounds and validation
  error budgets against the compiler/format audit. Approve the GPU/asset layouts
  and exact list of dual-format HDR products. Document section 6's regularization
  and unit equations in the owning lighting LLD.
- Register this delivery order in PLAN/status. Create `design/renderer-core/lightbench.md`
  for experiments and instrument requirements.

**Gate:** public behavior is specified by sections 3-7, GPU/asset layouts have
owners, and every active HDR path appears in the domain migration checklist.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 1 — Contracts | validated | Numeric domain, error budgets, layouts and HDR inventory have designated owners. | [Contract checkpoint](validation.md) |

## Supporting records

- [validation](validation.md)
