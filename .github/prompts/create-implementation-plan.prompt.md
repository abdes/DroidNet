---
mode: "agent"
description: "Plan scoped implementation slices with dependencies and measurable evidence"
---

# Create implementation plan

Plan `${input:PlanPurpose}` using applicable `AGENTS.md`, accepted requirements
and owning designs. For Oxygen, follow `design/oxygen/RULES.md` for milestone
location, stable IDs, status vocabulary and validation ownership.

- Extend an existing plan for the same scope. Otherwise use its established
  `plan/` or module/design location, not a mandatory global naming template.
  Keep one permanent plan per Oxygen milestone; do not create competing plans.
- Put a concise outcome/remaining/evidence summary first. Give slices stable IDs,
  affected files/projects, prerequisites, dependencies, implementation tasks and
  measurable workflow acceptance. Parallel work is possible only where dependencies
  permit it; do not require delegation or invent independent phases.
- Refer to symbols/paths rather than fragile line numbers. Include exact commands
  only after verifying the owning workflow. Separate managed builds, native
  dependency generation, engine builds and runtime checks; respect build scope
  authorization, especially for editor-only changes.
- Record decisions, meaningful rejected alternatives, risks and assumptions.
  Unresolved decisions need an owner/next action or a focused question, not a
  fabricated deterministic implementation. Obtain approval for contract changes.
- Track pending work with IDs, state, next action and owner when known. Do not mark
  implementation, documentation or a successful compile as validated acceptance.
  Keep detailed evidence beside the plan, not execution logs in architecture docs.
- Validate requirement-to-task coverage, dependencies and acceptance checks.
  Run file-scoped hooks and `git diff --check`; report remaining decisions and
  verification actually run. Creating a plan does not authorize its execution.
