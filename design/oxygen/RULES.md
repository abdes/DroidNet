# Oxygen engineering rules

Applies to all Oxygen engine, editor, interop, examples, tools and design work.
Read: [ownership](#ownership), [C++](#c), [editor](#editor),
[validation](#validation-and-delivery), [documentation](#documentation).

## Ownership

- Put behavior on its natural owning API. Respect module boundaries; fix an
  inadequate public API in its owner instead of adding pass-through free functions,
  ad hoc bridges, compatibility shims or parallel legacy paths.
- Inspect existing capabilities before inventing infrastructure: Base utilities,
  Nexus identity/publication/retirement, Graphics resources and the renderer upload
  and feedback services. Extract repeated mechanisms with real consumers; keep
  feature policy local. Future reuse informs design, not speculative frameworks.
- The editor owns authoring; the engine owns execution; interop exposes stable
  engine capabilities. Keep private headers, renderer internals and cooked binary
  layouts behind supported APIs. Native DLL discovery is bootstrap infrastructure.
- Build renderer features in Vortex. Retired `Oxygen.Renderer` is neither a fallback
  nor an implementation reference. Ground parity decisions in the relevant UE5.7
  source/shaders and cite those references in the owning design.
- Research non-obvious decisions, present concise alternatives and tradeoffs, and
  obtain approval before changing an accepted contract. Correct affected designs
  and plans before implementing changed scope.
- Before public release, tools and runtime readers accept only the current cooked
  formats defined by `Oxygen.Data`. Share those definitions; do not add private
  version pins, compatibility readers or legacy-format fallback paths. Recook
  maintained content when formats change.
- Validate data with schemas first; use manual checks only for constraints the
  schema cannot express.

## C++

- Write readable code that follows the repository style from the first edit.
  Use Base's `Macros.h`, `ScopeGuard`, `Result`, `observer_ptr`, `NamedType`, hash,
  enum and container helpers where appropriate. Check their actual definitions.
  Use the established copy/move macros and module export macros, including
  `OXGN_*_NDAPI` (for example, `OXGN_GFX_NDAPI`).
- Always brace control statements, initialize members, avoid redundant member
  initialization, select appropriately sized enum bases and declare complete
  special-member semantics. Initialize required aggregate fields explicitly.
  Avoidable deviations from these checks are forbidden:
  `readability-braces-around-statements`, `cppcoreguidelines-pro-type-member-init`,
  `readability-redundant-member-init`, `performance-enum-size`, and
  `cppcoreguidelines-special-member-functions`.
- Use C++20/23 when it improves clarity. Engine internals may use C++23; editor
  C++/CLI and native-command consumers require C++20. Verify installed headers,
  CMake `PUBLIC FILE_SET` and transitive includes: an `Internal/` directory does
  not make a header private. Keep C++23 types behind that boundary and use the
  existing Oxygen `Result` at editor-facing APIs; verify changed SDK surfaces
  with an installed-SDK consumer build.
- Prefer small named structs/enums, explicit state transitions and early returns.
  Use constrained templates only for genuine type variation; share common machinery
  out of line. Avoid boolean argument sequences, CRTP/policy matrices and callback
  collections when ordinary overloads or loops suffice.
- Use RAII and unique ownership by default; existing `ScopeGuard` handles rollback.
  Release/destructors must not throw or wait for the GPU. Shared/weak ownership
  needs an actual retained lifetime; avoid per-dispatch/poll control-block allocation,
  ownership cycles and unstable callback addresses.
- Borrow with spans only within the caller's lifetime; own data needed later.
  Check size/pitch arithmetic and byte layouts. Copy mapped bytes into aligned
  values with `memcpy`; do not reinterpret arbitrary addresses as typed objects.
- Preserve meaningful error distinctions: pending/busy, invalid input, budget,
  allocation, recording and uncertain submission are different outcomes. Use
  existing error families and convert once at service boundaries.
- Keep production shaders and hot paths free of test-only diagnostics. Balance
  numerical accuracy against visible quality and measured performance; do not
  pay substantial runtime cost for immaterial precision gains. Correctness
  failures still require fixes.

## Editor

- Minimize cognitive load: consistent alignment/proportions, progressive disclosure
  and no duplicated information. Open the Project Browser without a project and
  a usable workspace with one. Place authoring in the hierarchy, viewport,
  inspector and content browser, using precise authoring terminology.
- Complete applicable undo/redo, dirty state, persistence, live sync, cooking and
  validation together. Saved and live components must have the same semantics.
  Missing native APIs or field mappings are implementation work; an unsupported
  warning does not complete a required capability. Editor V0.1 scope and
  qualification follow [PRD sections 8–10](../editor/PRD.md).
- Keep authoring data, cooker input, cooked output and live state distinct. Prefer
  shared JSON descriptor schemas. Augment small schema gaps; document
  structural divergence in the owning LLD before adding a parallel schema.
- Hand descriptors/manifests to the cooker; do not hardcode cooked binaries or
  edit derived output as authoring data. Typed asset references preserve source,
  descriptor, generated, cooked and missing intent rather than flattening it.
- Durable settings require an owning architecture/LLD. Make invalid or unavailable
  runtime state and cook/mount/sync/render failures visible. Keep diagnostics in
  dedicated UI/logs and temporary instrumentation out of production INFO output.

## Validation and delivery

- Write clean C++ from the first edit and run `oxyformat` before building.
  Build and test during implementation; reserve `oxytidy` for the pre-commit
  check. Before requesting a commit, clear all warnings in modified C++ files,
  including pre-existing warnings and IDE diagnostics. Preserve unrelated user
  edits/settings. Do not suppress checks without an explicitly approved exception.
  Batch cleanup; repeated lint runs are not a substitute for careful coding.
- Use parallel `MSBuild.exe /m` for editor verification, not `dotnet`. For example:
  `MSBuild.exe projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj /nologo /m /p:Configuration=Debug /v:minimal`.
  Editor-only work does not authorize an engine build; obtain explicit authorization.
  For engine work, follow the task's agreed build/validation ownership.
- Select checks for the change; reuse applicable evidence and rerun affected checks
  when code or inputs change. Test observable behavior and failure recovery, not
  private call sequences. Avoid unrelated sweeps and redundant validation loops.

| Change                  | Evidence                                                                                       |
| ----------------------- | ---------------------------------------------------------------------------------------------- |
| Documentation           | Content coverage, links/navigation and formatting                                              |
| CPU contracts/lifetimes | Focused owning builds and tests                                                                |
| Shader or CPU/HLSL ABI  | Shader/catalog checks and relevant native tests                                                |
| GPU resources/visuals   | Runtime/debug-layer checks and analyzed captures; RenderDoc for resource/pass inspection       |
| Performance             | Fixed-input native Release measurements; Tracy for CPU/GPU attribution and resource accounting |

- State what changed, what was actually verified and what remains. Compilation or
  an unanalyzed capture is not workflow/visual/parity acceptance. Completion requires
  implementation, current designs and the required evidence; unrun required checks
  leave work incomplete. Correct inaccurate claims and affected status promptly.
- Commit only after the user has reviewed the changes and explicitly approved.

## Documentation

- Write for the next engineer: concise facts, rationale and useful rejected
  alternatives. Keep equations, tolerances and material limitations; remove
  defensive disclaimers, session narration and cleanup-history prose. Start long
  documents with a short linked summary.
- One fact, one owner: PRD owns requirements/exclusions; architecture owns boundaries;
  module LLDs own interfaces, algorithms, lifetimes, failure modes and exit gates;
  roadmaps own sequence/dependencies. Specify reusable capabilities in their owning
  module's docs, not a consuming feature's LLD.
- Each milestone has one permanent plan with stable IDs, numbered slices, affected
  files/projects, dependencies and measurable workflow acceptance. Put concise
  status/outcome, remaining work and evidence at the top. Keep detailed validation
  beside the plan; implementation history does not belong in an LLD.
- Track pending, incomplete, deferred and unresolved items in prioritized tables
  with stable IDs, state, next action and owner. Link them from the milestone.
  Consolidating documents does not deliver or defer requirements: preserve decisions,
  findings, significant details, open questions and evidence, and check source-to-target
  coverage. Give newly discovered gaps concrete tasks and pass/fail cases; retain
  historical proof at its original scope.
- Git holds working code and accurate docs, not debugging archives. Keep captures,
  traces, logs, source snapshots and binary bundles in ignored local output, never
  Git. Retain only compact proof or benchmark results useful for future comparison
  or establishing baselines, with reproduction commands, configuration and source
  identity. Commit reusable tests/tools; do not package routine successful runs.
- Editor: maintain one concise milestone validation summary in
  [IMPLEMENTATION_STATUS.md](../editor/IMPLEMENTATION_STATUS.md), not an execution log.
  Assign new fixes to named gap-closing milestones; do not reopen or supersede
  delivered milestones to absorb them.
- Vortex: milestone READMEs begin with `Status:` and an `Outcome`/`Remaining`/`Evidence`
  table; link unfinished IDs to `OPEN_ITEMS.md` and regenerate `STATUS.md`.
  Use `planned`, `in_progress`, `landed_needs_validation`, `blocked`, `validated`,
  `future`, `superseded` or `removed` accurately; the last three never mean delivered.
  Keep subdivision dots (`EX05.1`, `VTX-M04D.1`) and historical IDs unchanged.
  From `projects/Oxygen.Engine`, run `python tools/vortex/CheckDocumentation.py`
  and `git diff --check`; use `--write-status` for the index.
