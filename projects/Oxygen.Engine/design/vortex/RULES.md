# Vortex engineering and documentation rules

## Engineering

- Build Vortex-native systems. `Oxygen.Renderer` is retired and is not an
  implementation reference or fallback.
- Ground parity work in the relevant UE5.7 source and shaders under
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`. Record the references in the owning design.
- Update the design and milestone scope when a discovered gap changes the work.
  Record accepted departures with their technical reason and approval.
- Close a milestone when implementation exists, its designs are current, and
  the required validation passes. Record unrun checks and unresolved work explicitly.

## Document ownership

| Document                                     | Owns                                                                         |
| -------------------------------------------- | ---------------------------------------------------------------------------- |
| PRD                                          | Product requirements and exclusions                                          |
| Architecture, design overview, source layout | System boundaries, integration and placement                                 |
| LLD                                          | Equations, algorithms, interfaces, ownership, lifetimes and failure behavior |
| Roadmap                                      | Milestone navigation, sequence and dependencies                              |
| Generated status index                       | Read-only overview of the leading milestone status blocks                    |
| Open-items tracker                           | Pending, incomplete, deferred and unresolved work with stable IDs            |
| Milestone README                             | Scope, tasks, status, acceptance and delivered outcome                       |
| Milestone validation                         | Commands, conditions, results, limits and evidence references                |
| Evidence                                     | Immutable captured inputs, outputs, manifests and source snapshots           |

Keep a technical fact in its owning document and link to it elsewhere. A completed
milestone keeps its original home and stable IDs. Update its task rows in place;
keep removed, superseded and deferred requirements distinguishable from delivered work.
Archive superseded designs only when their historical rationale remains useful.

Keep open work in compact priority tables with a linked summary at the top.
Retain item IDs, state, next action and owner; keep cleanup history out of the tracker.

Use dots for subdivision folders: `EX05.1`, `EX08.2`, `VTX-M04D.1`.
Keep historical work-item IDs such as `EX051-10A` unchanged.

## Milestone template

Every milestone starts with `Status:` followed by a short table containing
`Outcome`, `Remaining` and `Evidence`. Link unfinished work to its stable ID in
`OPEN_ITEMS.md`. Regenerate `STATUS.md` after changing a leading status block;
do not maintain a second narrative status ledger.

Use these sections when they apply; a small milestone can remain one document:

1. **Outcome and scope:** intended behavior, meaningful exclusions, status.
2. **Dependencies and designs:** prerequisites and links to technical owners.
3. **Tasks:** ordered, independently reviewable work with stable IDs and status.
4. **Acceptance:** measurable behavior and required checks.
5. **Outcome:** delivered result, remaining work and accepted decisions.
6. **Validation:** inline results or a link to the adjacent validation record.

Write for the next engineer. Remove repeated policy, session instructions and
approval narration. Retain design rationale, equations, tolerances, benchmark
conditions, failed experiments, accepted limitations and deferred work.

## Status vocabulary

| Status                    | Meaning                                                                  |
| ------------------------- | ------------------------------------------------------------------------ |
| `planned`                 | Scope defined; implementation has not started                            |
| `in_progress`             | Work or required validation remains                                      |
| `landed_needs_validation` | Implementation exists; final qualification remains                       |
| `blocked`                 | A required decision, dependency or proof surface is missing              |
| `validated`               | Implementation, current designs and required evidence satisfy acceptance |
| `future`                  | Deferred beyond the scheduled delivery                                   |
| `superseded`              | Replaced or merged; link to the current owner                            |
| `removed`                 | Explicitly removed from the accepted scope                               |

## Validation

Select checks for the changed behavior. Record commands, configuration, source
and runtime identities, observed results, and remaining gaps. Reuse applicable
evidence; new code or changed inputs need the relevant checks again.

| Change                                | Required evidence                                                           |
| ------------------------------------- | --------------------------------------------------------------------------- |
| Documentation                         | Content coverage, local links, milestone navigation, formatting             |
| CPU contract or lifetime              | Focused build and owning tests                                              |
| Shader or CPU/HLSL ABI                | Shader build/catalog checks and relevant native tests                       |
| GPU pass, resource or visual behavior | Runtime/debug-layer checks and analyzed captures                            |
| Performance                           | Release measurements with fixed inputs, attribution and resource accounting |

An unanalysed capture or successful compile does not establish rendering parity.
Distinguish measured, visually accepted and deferred outcomes in the result.
Store each raw result once; derived reports reference its path, identity and hashes.
Never edit captured evidence to match a later checkout. Original paths inside
historical manifests describe the recorded run; the migration map locates moved files.

## LLD contents

Describe scope, public contracts, data flow, resources and lifetime, shader ABI
where applicable, frame integration, validation approach and unresolved decisions.
Implementation progress and historical test runs belong in the milestone package.

## Documentation checks

From `projects/Oxygen.Engine`, run:

```powershell
python tools/vortex/CheckDocumentation.py
git diff --check
```

The checker verifies links, navigation and immutable evidence hashes. The commit
hook runs these checks and the normal formatters. During a document migration,
add `--migration` to compare retained content with the recorded original revision;
ordinary design edits do not require the text to remain identical to that baseline.
Use `--write-status` to refresh the generated progress index. The checker also
requires unique open-item IDs and an entry for each deferred editor capability.
