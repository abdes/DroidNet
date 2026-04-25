# Vortex Milestone Planning Workflow

Status: `planning standard`

This workflow defines how any Vortex milestone should be prepared before
implementation starts. It is written for an engineer or AI agent who needs to
turn a roadmap milestone into a reliable, verifiable implementation plan.

## 1. Inputs

Every milestone plan starts from these inputs:

1. The milestone row in [../PLAN.md](../PLAN.md).
2. The current status row and open gaps in
   [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).
3. The owning LLD or LLD set under [../lld/](../lld/README.md).
4. Current source reality in the Vortex module and Vortex shader tree.
5. Relevant UE5.7 source and shader references.

If any input contradicts another, stop and correct the plan or design before
implementation starts.

## 2. Required Output

A detailed milestone plan must answer these questions:

| Question | Required Answer |
| --- | --- |
| What is the exact milestone ID? | Use the stable ID from `PLAN.md`; do not invent a new phase label. |
| What is in scope? | Name the runtime behavior, contracts, publications, shaders, tests, and proof surfaces. |
| What is out of scope? | Explicitly exclude adjacent work that must not leak into the milestone. |
| What exists already? | Separate implementation-present surfaces from validated surfaces. |
| What must be preserved? | Name invariants and existing behavior that must not regress. |
| What will be changed? | Split work into small slices with clear ownership. |
| What proves closure? | Define build, test, runtime, shader, UE5.7, and capture evidence. |
| What blocks closure? | List missing dependencies, unresolved design questions, and accepted gaps. |

## 3. Planning Steps

### Step 1 — Confirm Scope

Read the milestone row in `PLAN.md` and copy its purpose into the detailed
plan. Then refine it into:

- primary goal
- first implementation slice
- dependencies
- non-scope
- exit gate

Do not expand the milestone to absorb unrelated future work.

### Step 2 — Reconcile Current State

Inspect current source before assuming absence or completion. Classify every
relevant surface as:

- `validated`
- `landed_needs_validation`
- `in_progress`
- `planned`
- `future`

Use `landed_needs_validation` for real code that lacks fresh proof in
`IMPLEMENTATION_STATUS.md`.

### Step 3 — Ground The Parity Contract

Identify the UE5.7 source and shader families that define the target behavior.
The plan must name the families, algorithms, shader techniques, and authored
parameters relevant to the milestone.

Do not use legacy `Oxygen.Renderer` as a parity reference.

### Step 4 — Define Contract Truth

For every CPU/GPU publication affected by the milestone, define:

- producer
- consumer
- valid state
- invalid state
- disabled state
- stale state
- diagnostics/inspection surface
- CPU/HLSL ABI implications

A binding, slot, counter, revision, or flag must not imply more runtime behavior
than the renderer actually produced.

### Step 5 — Slice Implementation

Prefer narrow slices that can be verified independently. A good slice changes
one contract or one behavior family and has a focused test gate.

Each slice should include:

- purpose
- primary code areas
- expected tests
- validation command
- status update requirement
- rollback/replan trigger

### Step 6 — Define Verification Before Coding

The plan must define verification before implementation starts.

At minimum:

- focused build target
- focused test filter
- shader bake/catalog validation if shaders or CPU/HLSL ABI change
- runtime proof command if a runtime scenario is in scope
- RenderDoc capture/replay/analyzer proof if GPU pass ordering, descriptors, or
  visual output are part of the claim
- status update requirements

### Step 7 — Record Closure Rules

A milestone cannot close unless:

1. implementation exists
2. required design/status docs are updated
3. validation evidence is recorded in `IMPLEMENTATION_STATUS.md`
4. UE5.7 parity evidence is recorded for parity claims
5. residual gaps are either blockers or explicitly accepted by the human

## 4. Required Plan Template

Use this section shape for detailed milestone plans.

```md
# <Milestone ID> — <Milestone Name>

Status: `planned` or `in_progress`

## 1. Goal
## 2. Scope
## 3. Non-Scope
## 4. Current State
## 5. Existing Behavior To Preserve
## 6. UE5.7 Parity References
## 7. Contract Truth Table
## 8. Implementation Slices
## 9. Test Plan
## 10. Runtime / Capture Proof
## 11. Exit Gate
## 12. Replan Triggers
## 13. Status Update Requirements
```

## 5. Anti-Patterns

- Marking a milestone complete because code exists.
- Treating a revision counter as proof that a resource was produced.
- Hiding work inside a generic cleanup milestone.
- Using broad historical phase labels instead of concrete milestone IDs.
- Adding private inspection hooks instead of published diagnostics contracts.
- Letting tests pass because disabled or invalid states silently skip behavior.
- Skipping UE5.7 source/shader grounding for parity work.
- Creating a plan that does not name its validation commands.

## 6. Reader Checklist

A plan is implementation-ready when a fresh reader can answer:

- What do I implement first?
- What files/modules will I likely touch?
- What behavior must I not regress?
- What exact proof do I need before closure?
- What remains out of scope even if it is nearby?
- What should I update in `IMPLEMENTATION_STATUS.md` after verification?
