# Oxygen Editor Rules

Status: `active rules`

These rules apply to all Oxygen Editor design and implementation work.

## 1. Architectural Rules

1. The editor owns authoring workflows; the engine owns runtime execution.
   Neither side should absorb the other's responsibilities.
2. The editor must not paper over engine API problems with ad hoc native bridge
   code. If a public Oxygen Engine API leaks implementation details, requires a
   newer C++ mode than the editor can consume, or exposes an unstable contract,
   fix the engine API.
3. The managed editor must not depend on engine-private headers, internal
   renderer implementation details, or cooked-file binary structures except
   through supported Oxygen APIs.
4. Process-local native runtime discovery for loading engine DLLs is bootstrap
   infrastructure, not product configuration.
5. The interop layer is a narrow boundary for stable engine capabilities. It is
   not a dumping ground for temporary behavior, policy, or authoring logic.
6. Authoring scene data, cooker input, cooked scene data, and live engine state
   are distinct products. They may be synchronized, but must not be confused.
7. Every new editor feature must define how it participates in undo/redo,
   dirty state, persistence, live-engine sync, cooking where applicable, and
   validation.
8. Do not introduce durable editor settings as incidental feature plumbing.
   Settings surfaces require a documented settings architecture and an owning
   LLD before implementation.

## 2. User Experience Rules

1. When no project is open, the first experience is the Project Browser. When
   a project is open, the first editor experience is the usable workspace, not
   a marketing surface.
2. Scene authoring operations must be available from natural editor locations:
   hierarchy, viewport, inspector, and content browser.
3. Property editors should expose engine concepts in authoring terms while
   retaining enough precision for technical users.
4. The editor must make invalid or unavailable runtime state visible. Silent
   failure is not acceptable for cooking, mounting, sync, or rendering.
5. Diagnostics belong in dedicated UI and logs. Temporary diagnostics must not
   permanently pollute engine code or runtime output at INFO level.

## 3. Build And Verification Rules

1. Use `MSBuild.exe` for editor builds. Do not use `dotnet` for this repo's
   editor verification workflow.
2. Use parallel MSBuild by default:

   ```powershell
   MSBuild.exe projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj /nologo /m /p:Configuration=Debug /v:minimal
   ```

3. Do not build `Oxygen.Engine` unless explicitly asked. Engine verification is
   performed by the engine owner unless requested otherwise.
4. A milestone is not complete because the project compiles. It must close the
   relevant workflow validation gate.
5. [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md) records one concise
   validation summary per milestone. It must not become an append-only
   execution log.

## 4. Data Contract Rules

1. Prefer engine JSON descriptor schemas as editor-authored persisted data when
   they fit the authoring model.
2. The editor may diverge from engine descriptor schemas only through an
   explicit design decision that documents why the engine schema does not fit.
3. If the mismatch is small, prefer augmenting the engine schema over creating
   a parallel editor-only schema.
4. If the mismatch is structural or repeated, evaluate the editor/engine schema
   relationship in the relevant LLD before implementation.
5. Cooked output is a derived product. The editor may inspect it, mount it, and
   validate it, but must not edit cooked assets as authoring data.
6. Import descriptors and manifests are the preferred handoff to the cooker.
   Hardcoded cooked binary generation is not an acceptable long-term solution.
7. Live engine sync must use the same conceptual component semantics as saved
   authoring data. A component that saves but does not sync is incomplete.
8. Asset references must preserve authoring intent. Do not collapse source,
   descriptor, generated, cooked, and missing references into raw strings.

## 5. Planning Rules

1. LLDs must include ownership, data contracts, failure modes, and exit gates.
2. Work packages must identify files/projects likely to change.
3. Roadmap items must be measurable by user workflow, not only by code surface.
4. Current-state claims must match the repository. If implementation changes,
   update [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md).
