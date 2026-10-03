---
mode: "agent"
description: "Update an owning specification without losing accepted contracts or evidence"
---

# Update specification

Update `${file}` for the requested requirements or code changes, following
[specification guidance](create-specification.prompt.md) and applicable module
rules. Do not move it to a new `/spec/` hierarchy or replace its established
structure with a generic template.

- Read the existing specification, referenced contracts and affected code. Identify
  whether the discrepancy is an implementation gap or an approved contract change;
  do not silently weaken requirements to match incomplete behavior.
- Preserve stable IDs, rationale, significant alternatives, unresolved items and
  historical evidence at its original scope. Change status only when current
  acceptance evidence supports it; reorganization alone is not delivery.
- Update affected contracts, examples, acceptance checks and cross-references
  together. Keep requirements, design, plans and validation in their owning docs
  rather than copying the same fact everywhere.
- Summarize contract changes, approval needs, remaining gaps and checks actually
  run. Validate changed documents with file-scoped hooks and `git diff --check`.
