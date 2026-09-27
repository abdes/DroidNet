# Vortex engineering and documentation rules

Read: [engineering](#engineering), [document ownership](#document-ownership),
[C++ for shared infrastructure](#c-for-shared-infrastructure).

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

## C++ for shared infrastructure

Apply this guidance when introducing or extracting shared engine utilities.
It is not a requirement to retrofit unrelated existing code.

The [engine toolchain](../../cmake/ToolchainRequirements.cmake) supports
C++23; the editor's C++/CLI and native-command projects use C++20. Keep new helpers
in engine/internal include graphs. Existing `IndexReuse` already uses
`std::expected`; shared-utility work does not migrate the entire Nexus API or raise the editor's
language mode. An `Internal/` path is not a privacy boundary: current module
CMake lists install some internal headers. Check `PRIVATE` sources versus
`PUBLIC FILE_SET` and the transitive installed include graph explicitly; keep
new C++23 types out of the C++20 Renderer/Interop surface. Validate with the
installed SDK consumer build, without a legacy-header sweep.
Editor-facing headers continue to use the existing Oxygen
`Result` where needed, with internal implementation types hidden.

| Technique                                              | Application                                                                                                                                                                                     | Keep the code simple                                                                                                                                                                                                                                         |
| ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Move-only RAII                                         | Prepared upload, feedback reservation and Nexus retirement ticket clean up on every early return. Use the existing `ScopeGuard` for local rollback.                                             | Explicit prepare → record → resolve flow; no templated typestate pipeline. Release/destructors are nonthrowing and never wait for the GPU.                                                                                                                   |
| `std::span<const T>` and value descriptors             | Borrow texels, view requests and copy plans for synchronous preparation. Copy the small immutable data needed by later work into its owner.                                                     | Never retain caller spans, references or range views past their lifetime. Preserve checked pitches, sizes and byte-layout assertions. Copy mapped bytes into an aligned local payload with `memcpy`; do not reinterpret an arbitrary mapped address as `T*`. |
| `std::expected` internally; existing boundary `Result` | Distinguish busy, invalid input, allocation/registration and recording/submission errors. A typed poll can return `expected<optional<T>, ReadbackError>` for pending/payload/transport failure. | Match existing error families; convert once at the service boundary. Do not flatten device uncertainty or budget rejection into ordinary allocation failure. Prefer early returns to long monadic chains.                                                    |
| Small enums and named structs                          | Describe reservation phase, finalization outcome, view request and initialization result explicitly.                                                                                            | Avoid boolean parameter sequences and unrelated success flags. Keep expected errors separate from valid-but-incomplete payloads.                                                                                                                             |
| Constrained templates                                  | `IndexLike` for Nexus indices and a trivially-copyable payload constraint for typed feedback. Use fixed-capacity storage where the owning limit is fixed.                                       | Share non-type-dependent machinery out of line; avoid CRTP, policy matrices and type-erased callback collections. Separate texture/buffer overloads are clearer than a universal resource template.                                                          |
| `unique_ptr`, `shared_ptr`, `weak_ptr`                 | Unique ownership for helpers; shared ownership only for genuine concurrent/retained readers; weak eligibility for returns to expired owners.                                                    | Do not add control-block allocations per dispatch/poll or capture Graphics-owning CPU pools in GPU-retained work. Preserve stable callback addresses.                                                                                                        |
| `constexpr`, `std::array`, simple range algorithms     | Small fixed metadata, capacities and readable searches.                                                                                                                                         | Ordinary loops are preferred for state transitions and fused work. No custom allocator, coroutine scheduler, `mdspan` ABI layer or reflection/code generator is required.                                                                                    |

A shared operation should make both real adopters easier to read. Keep domain
checks beside the service state they explain; extract the repeated mechanism
rather than parameterizing whole services. Comments explain ownership, ordering
or a non-obvious constraint. Tests describe observable behavior and failure
recovery, not the helper's private sequence of method calls.
