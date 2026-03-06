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
