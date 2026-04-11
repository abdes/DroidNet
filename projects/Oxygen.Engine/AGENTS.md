# Oxygen.Engine Agent Guardrails (Non-Negotiable)

These rules are mandatory for every task in this repository.

## 1. No-False-Completion Gate

The agent MUST NOT claim phase/task completion unless:

1. Implementation exists in code.
2. Required docs/plan were updated.
3. Validation evidence is available and explicitly stated.

If build/test execution is not performed (or not allowed), the status MUST remain `in_progress` and the missing validation delta MUST be listed.

## 2. Scope-Drift Blocker

If the agent discovers that the current design/impl scope is insufficient for the requested parity:

1. Stop implementation claims immediately.
2. Update design + implementation plan docs first.
3. Mark previous status as incomplete where needed.
4. Continue only after documenting the corrected scope.

## 3. Truthfulness and Evidence

Every progress/completion statement MUST be evidence-backed:

1. Files changed.
2. Tests run (or explicitly not run).
3. Remaining gap to exit gate.

No implicit "done". No hidden assumptions. No bluffing.

## 4. Violation Handling Protocol

If the agent violates rules 1-3:

1. Explicitly acknowledge the violation.
2. Identify impacted decisions/status claims.
3. Apply corrective doc/status patch in the same iteration.
4. Re-state remaining work before proceeding.

## 5. Architectural Discipline

1. No shortcuts or parallel legacy paths unless explicitly approved.
2. Reuse existing engine pipelines/helpers before adding new mechanisms.
3. Keep domain ownership boundaries clean (no cross-domain option payload leakage).
4. Use schema-first validation; manual validation only for non-schema-enforceable constraints.

## 6. Proof-Carrying Progress

This repository prefers **proof-carrying progress** over either bluffing or defensive stagnation.

1. Do the substantive work first; do not substitute process talk for progress.
2. Do not make strong claims (`done`, `fully preserved`, `fully reviewed`, `complete`, equivalent) unless the corresponding verification has already been performed.
3. Bundle every strong claim with its proof artifact:
   changed files, tests/results, coverage check, source-to-target mapping, or exact residual gap.
4. If self-review finds gaps, fix them before reporting unless blocked.
5. Avoid vague confidence language like `appears`, `looks`, or `should be fine` when a binary verified/not-verified answer is possible.
6. Keep verification discipline mostly internal; surface the result and the evidence, not a wall of defensive meta-commentary.
7. When splitting, migrating, or rewriting documents, preserve architecture decisions, implementation-significant details, simulation findings, deferred items, and explicit open questions; run a source-to-target coverage check before claiming preservation.
