# Vortex Rules

This document centralizes the mandatory execution rules for the Vortex design
package. Use it when a task contract or planning artifact refers to
`design/vortex/RULES.md`.

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

## Related Truth Surfaces

- [PRD.md](./PRD.md) — stable product requirements
- [ARCHITECTURE.md](./ARCHITECTURE.md) — stable structural architecture
- [DESIGN.md](./DESIGN.md) — top-level design entry point
- [PLAN.md](./PLAN.md) — active execution plan and phase ordering
- [IMPLEMENTATION-STATUS.md](./IMPLEMENTATION-STATUS.md) — current-state and
  resumability ledger
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) — target file-placement reference
- [lld/README.md](./lld/README.md) — low-level design package index

## Usage Notes

- `IMPLEMENTATION-STATUS.md` is the current-state truth surface when a planning
  document and the live branch disagree.
- `PLAN.md` remains the roadmap and claim surface; do not treat it as proof of
  implementation by itself.
- `PROJECT-LAYOUT.md` defines intended placement and organization, not proof
  that every illustrated future file already exists on the branch.

## Ledger Rules

1. **Evidence, not intention.** Every entry records what exists in code and
   what validation was run, not what was planned or discussed.
2. **No-false-completion.** A phase is `done` only when: code exists,
   required docs are updated, and validation evidence is recorded here.
3. **Missing-delta explicit.** If build or tests were not run, the phase
   stays `in_progress` with the missing validation delta listed.
4. **Scope-drift trigger.** If scope changes or the current design is found
   incomplete, update design docs before claiming further progress.
5. **Per-session update.** Each implementation session must update this file
   with: changed files, commands run, results, and remaining blockers.
