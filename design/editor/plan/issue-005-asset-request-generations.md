# Issue #5: Reject Superseded Asset Load Completions

Status: `implemented; automated validation complete`

Issue: [#5](https://github.com/abdes/DroidNet/issues/5).

Design owners: [live-engine-sync.md](../lld/live-engine-sync.md) and
[runtime-integration.md](../lld/runtime-integration.md).

## Verified Findings

- `SetGeometryCommand` and `SetMaterialOverrideCommand` capture a scene node in
  their load callbacks. They check lifetime but do not check whether a newer
  request superseded the load. Both callbacks mutate the scene directly.
- Cached/procedural replacement, material clear, and `DetachGeometryCommand`
  do not invalidate pending callbacks. Undo uses these same synchronization
  paths, so it is subject to the same race.
- `EditorModule::OnSceneMutation` already executes commands on the permitted
  mutation path. Completion callbacks must hand results to that path rather
  than retaining a command context or mutating nodes themselves.
- Native node handles contain a resource generation and scene ID, but scene
  IDs can be reused. An explicit scene-session lifetime is also required.
- The engine preserves material overrides by LOD/submesh index when geometry
  changes. Detach removes the renderable component and its overrides.
- The current live-sync contract reports `Accepted` when a command is queued.
  Subsequent resolution/load failures go to native runtime logging and must
  not roll back authored data.

## Required Behavior

1. Every geometry request supersedes the previous geometry request for that
   node, including cached, procedural, unresolved, and failed requests.
2. Every material request or clear supersedes the previous request for that
   node and slot. Independent slots and nodes remain independent.
3. Detach invalidates pending geometry and all material requests for that
   renderable, even when the component has not yet been attached. Reattachment
   cannot revive requests from the detached component lifetime.
4. Geometry replacement preserves the existing slot-index override contract.
   It must not indiscriminately cancel unrelated material intent. A current
   material result awaiting the requested geometry must not be silently lost
   merely because its slot does not exist in the previous geometry.
5. Destroyed nodes, deleted descendants, replaced scenes, and shutdown reject
   pending completions. Callbacks must neither retain the scene/module nor
   dereference them after their lifetime ends.
6. Completion acceptance and scene mutation occur together during
   `kSceneMutation`, after checking scene/component lifetime, full node handle,
   and the relevant request generation. No check-then-queue acceptance window.
7. Current load failures include asset/target context in native diagnostics.
   Superseded results are discarded without reporting failure against the
   current authoring request. Queue acceptance retains its existing meaning.

## Implementation Plan

1. Add scene-session-owned asset request state in the interop editor module:
   geometry generations per node, material generations per node/LOD/slot,
   renderable lifetime invalidation, and a lifetime-safe completion inbox.
   Reuse the existing thread-safe queue abstraction. Keep the state bounded by
   removing dead targets and retiring scene sessions.
2. Register intent before resolver/cache/procedural branches in both commands.
   Loader callbacks capture only immutable request identity, the loaded result,
   and a weak completion destination. They never capture raw module/context
   pointers or apply to a captured node.
3. Drain results on the scene-mutation path and revalidate before applying.
   Process pending authoring commands before accepting completion results so
   newer commands already waiting for that phase supersede earlier loads.
   Preserve current material intent across geometry readiness and validate slot
   availability against the accepted geometry.
4. Invalidate component requests in detach, retire node requests on deletion
   (including descendants), and retire the entire session on scene teardown,
   replacement, and module shutdown. Undo/redo needs no separate generation
   mechanism because it issues the same commands.
5. Use the existing native diagnostic path for resolution/load/apply failures,
   with sufficient asset and target context. Do not introduce an additional
   managed completion API or change the `Accepted` contract for this fix.
6. Update both design-owner documents with the implemented contract and record
   validation evidence here before claiming completion.

## Focused Validation

Use controlled loader callbacks against the production commands and completion
drain, with actual scene nodes. Avoid timing sleeps and tests of counters alone.

- Geometry A/B and material A/B completed in both orders.
- Pending geometry replaced by cached and procedural geometry.
- Pending material replaced by a cached material or cleared.
- Clear of a visible override while replacement geometry is still loading;
  clear takes effect immediately and remains clear after replacement.
- Detach before geometry completion; detach/reattach before old material
  completion; clear or detach while the component is still absent.
- Undo and redo represented by the actual replacement/clear/detach commands.
- Node deletion/recreation, descendant deletion, scene replacement, and late
  callback after session shutdown.
- Independent nodes and material slots; material completion before geometry
  readiness; existing override preservation during geometry replacement;
  removed slots do not revive historical overrides if their index returns.
- Current load failure and superseded failure diagnostic behavior.
- A callback invoked outside scene mutation changes no scene state until the
  mutation drain; a newer queued authoring command wins before that drain.

Build with `MSBuild.exe /m` using the repository's existing output locations.
Run the focused native tests and relevant existing interop tests. Check modified
files for compiler and analysis diagnostics; if C# files change, include active
IDE suggestions in the audit. Do not rebuild or reinstall Oxygen.Engine: ask
the user to do that if engine source/API changes become necessary.

## Implementation and Evidence

- Added native `SceneAssetRequests` with per-target generations, weak completion
  inbox ownership, deferred material application, contextual diagnostics, and
  dead-target pruning. Session-wide monotonically increasing request IDs prevent
  detach/reattach from reusing a retired request's identity.
- Geometry/material commands now use that authority for all loading paths.
  Detach invalidates both kinds of request before removing the component.
- `EditorModule` drains results after authoring commands and retires the session
  before scene replacement/teardown and module destruction.
- Controlled-loader tests compile the production commands and request state
  into the existing native test project. Tests use real scenes and assets, with
  explicitly released callbacks rather than sleeps. Native fixture headers are
  compiled outside `/clr` to preserve native callback ABI across translation
  units.
- Debug interop and editor integration builds passed with `MSBuild.exe /m`
  using the existing output locations. Final editor build:
  `artifacts/issue5-editor-final-build.log`.
- All 34 native tests passed, including 19 issue #5 regression cases. Final
  evidence: `artifacts/issue5-native-final-build.log`,
  `artifacts/issue5-native-final-tests.log`, and the TRX under
  `artifacts/issue5-test-results/` dated `2026-09-10_18_06_25`.
- Native source and tests were built with C++ code analysis enabled. An audit
  of 52 native analysis XML reports found zero findings in modified files.
  Evidence: `artifacts/issue5-analysis-summary.log` and
  `artifacts/issue5-audit-analysis.py`. No C# files changed. Existing diagnostics
  in untouched repository files and installed third-party headers remain.
- `git diff --check` passed. Design-owner documents reflect the implemented
  contract. Clear acts immediately on visible geometry; successful material
  overrides are subsequently owned by the renderable, avoiding both retained
  asset ownership and historical override restoration after slot removal.
- No engine source files changed and no engine rebuild/reinstall was performed.
  Running-editor interaction replay has not been performed.
