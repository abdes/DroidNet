---
mode: "agent"
description: "Define a scoped specification with explicit contracts and acceptance criteria"
---

# Create specification

Create a specification for `${input:SpecPurpose}`. Follow applicable `AGENTS.md`
and, for Oxygen, `design/oxygen/RULES.md`. Read existing owning designs and actual
contracts before proposing a new document or API.

- Extend the owning document when possible; place a new specification in the
  established module/design hierarchy, not an invented root `/spec/` directory.
  Do not duplicate PRD requirements or module contracts into a parallel authority.
- Distinguish current behavior, proposed requirements, constraints and unresolved
  decisions. Preserve established IDs; assign stable IDs to new requirements and
  link each to measurable acceptance criteria.
- Cover only applicable sections: purpose/scope, definitions, requirements,
  interfaces/data contracts, ownership/lifetimes, failure/recovery, acceptance,
  dependencies and rationale. Include concrete edge cases/examples when useful;
  omit empty templates and speculative dependencies.
- Link related authorities instead of demanding self-containment through copied
  text. Record meaningful alternatives and tradeoffs; obtain approval before
  changing an accepted contract. Do not present uncertain choices as decisions.
- Define evidence appropriate to the change: managed MSTest/MTP, native
  GoogleTest/CTest, runtime/GPU checks or measurements. Do not invent GitHub CI,
  coverage thresholds or completed validation.
- Check source-to-spec coverage and relative links, then run file-scoped hooks
  and `git diff --check`. Report remaining questions and verification actually run.
