# Engine content pipeline review sets

Status: proposal

Last updated: 2026-11-01

The portable-references plan ends its engine work with S6f. From S7 on, the
editor is the content pipeline's main consumer: it reads cooked formats in
`Oxygen.Managed.Assets`, calls the runtime through `Oxygen.Editor.Interop`,
drives ImportTool, PakTool and Inspector as processes, and validates its own
files against the schemas the SDK installs. This document splits the engine
code the editor depends on into review sets for an external review before
that work starts. Each set lists its files, what the review must establish,
and the points that decide whether the code is correct.

Paths are relative to `projects/Oxygen.Engine/src/Oxygen/`. The sets cover
every production file of Serio, Data, Content, Cooker and SceneInstantiation
plus the Player launch path. Vendored code (`cgltf`, `ufbx`, `stb`,
`mikktspace`, `bc7enc`) and generated tables are out of scope; their wrappers
are in scope. Line counts are approximate and exclude tests.

## Contents

- [How to run a set](#how-to-run-a-set)
- [Checks for every set](#checks-for-every-set)
- [Review prompt](#review-prompt)
- [The sets](#the-sets)
- [R1. Serio IO](#r1-serio-io)
- [R2. Binary formats](#r2-binary-formats)
- [R3. Asset model and descriptor readers](#r3-asset-model-and-descriptor-readers)
- [R4. Mounts, precedence and dependency checks](#r4-mounts-precedence-and-dependency-checks)
- [R5. Loading, residency and instantiation](#r5-loading-residency-and-instantiation)
- [R6. Cooked roots: write, read, verify](#r6-cooked-roots-write-read-verify)
- [R7. Source mounts, references and model records](#r7-source-mounts-references-and-model-records)
- [R8. Manifests, requests and source analysis](#r8-manifests-requests-and-source-analysis)
- [R9. Import execution](#r9-import-execution)
- [R10. Textures](#r10-textures)
- [R11. Model adapters and node identity](#r11-model-adapters-and-node-identity)
- [R12. Geometry, buffers and materials](#r12-geometry-buffers-and-materials)
- [R13. Scenes and physics](#r13-scenes-and-physics)
- [R14. Scripting, input and audio](#r14-scripting-input-and-audio)
- [R15. Pak build and pak tools](#r15-pak-build-and-pak-tools)
- [R16. Tool command lines and SDK contracts](#r16-tool-command-lines-and-sdk-contracts)
- [R17. Test kits](#r17-test-kits)

## How to run a set

- **A code review, not a validation.** A set is handed over only when it
  builds and every test passes. The reviewer never builds, runs or modifies
  anything: tests are read as code, for what they prove and for what could
  break without one failing. A point reading cannot settle is Untested, with
  the evidence that would settle it.
- Read the set's documents first, then its public headers, then the
  implementation, then its tests.
- Report each finding with its file and line, a concrete failing input or
  sequence, its severity (defect, contract gap, test gap, debt) and the
  smallest fix. A finding without a failing scenario is a question, not a
  defect.
- State for each critical point whether it holds, fails or is untested.
- Review against the engine rules: [Oxygen engineering rules](../../../../design/oxygen/RULES.md),
  `Core/Constants.h`, the C++ style and test conventions in `.github/instructions`.

## Checks for every set

- **Untrusted input.** Every count, offset, size and index read from a file or
  a JSON document is bounded before use, including multiplication overflow.
  No read past a buffer, no allocation sized by an unchecked field.
- **Typed failures.** Every refusal has a stable diagnostic code with the data
  that explains it (path, object, expected and found). Codes are owned by one
  place; no two codes name one rule, and no generic error hides a specific
  one.
- **One owner per rule.** A grammar, a layout or an identity rule has one
  implementation. Flag copies, fallbacks, legacy branches and converters of
  old forms.
- **Docs match code.** Each listed document states the behaviour the code has,
  in the current vocabulary (mount, cooked source, loose cooked root, pak,
  generation, requirement); flag stale text and old forms.
- **Unfinished capability.** TODOs, stubs and unwired fields are listed with
  whether they are reachable from a manifest, a CLI or the runtime API.
- **Tests by contract.** Tests assert codes and contract outcomes, use the
  shared kits (R17) and do not lay out cooked bytes by hand outside a format's
  own reader tests.
- **Concurrency.** Shared state names its owner thread or lock; coroutines do
  not hold references across suspension points they do not own; cancellation
  leaves no partial publication.

## Review prompt

A set's prompt for an external reviewer is this template, with the set's
section filled in for the placeholders. The constraints and the report format
never change.

```markdown
# Code review: Oxygen engine <set title> (review set <Rn>)

You are an expert C++23 reviewer of <the set's domain>. You review one
subsystem of the Oxygen game engine before its main consumer, a WinUI
editor, depends on it. Judge the code on its merits, not on its comments.

## Context

<What the subsystem does, who calls it, and how the editor depends on it:
the set's row in "The sets".>

Engine root: `projects/Oxygen.Engine`; paths are relative to
`projects/Oxygen.Engine/src/Oxygen/` unless they start with `projects/` or
`design/`.

## Scope

<The set's Files, in reading order: docs, public headers, implementation,
tests.>

Review against `design/oxygen/RULES.md`, `src/Oxygen/Core/Constants.h`,
`.github/instructions/cpp_coding_style.instructions.md` and
`unit_tests.instructions.md`.

## Objectives

<The set's Objectives.>

## Critical points

Report each as Holds, Fails or Untested, with file:line evidence and, for
Fails, a concrete failing input or sequence.

<The set's Critical points, numbered.>

Across the set, also apply "Checks for every set": untrusted input, typed
failures, one owner per rule, docs matching code, unfinished capability,
tests by contract, concurrency.

## Constraints

- This is a code review, not a validation. The set builds and every test
  passes. Do not build, run or modify anything. Judge the tests by reading
  them: what each proves, and what could break without a test failing.
- Never guess. If reading the code cannot settle a point, mark it Untested
  and say what evidence would settle it.

## Report format

1. Summary: at most 5 lines; the verdict and the three most important
   findings.
2. Critical points table: number, Holds, Fails or Untested, one-line reason
   with file:line.
3. Findings, most severe first: severity (Defect, Contract gap, Test gap,
   Debt), file:line, the failing scenario, the smallest fix and the test that
   would prove it.
4. Questions: what looks wrong but could not be proven, with the evidence
   that would settle it.
5. Not reviewed: anything in scope skipped, and why.
```

## The sets

The order is the review priority: the contracts the editor consumes directly
come first.

| Set | Subsystem                                | Lines  | Editor depends on it through                          |
| --- | ---------------------------------------- | ------ | ----------------------------------------------------- |
| R2  | Binary formats                           | 6 800  | `Oxygen.Managed.Assets` readers (S7)                  |
| R4  | Mounts, precedence, dependency checks    | 15 200 | Live Preview and Play through the interop             |
| R7  | Source mounts, references, model records | 5 600  | Projects, records, reference check (S8 to S13)        |
| R16 | Tool command lines and SDK contracts     | 12 600 | ImportTool, Inspector processes and installed schemas |
| R6  | Cooked roots: write, read, verify        | 7 600  | Cook publication and incremental cooks (S10)          |
| R8  | Manifests, requests, source analysis     | 19 200 | Manifests the editor writes; analysis diagnostics     |
| R1  | Serio IO                                 | 8 400  | Every file read and atomic publication                |
| R3  | Asset model and descriptor readers       | 14 600 | Scene, geometry and material types in the interop     |
| R5  | Loading, residency, instantiation        | 10 000 | Preview loads, hot reload, scene instantiation        |
| R9  | Import execution                         | 8 900  | Cancellation, progress and reports of every import    |
| R15 | Pak build and pak tools                  | 16 600 | Pak build (S16)                                       |
| R11 | Model adapters and node identity         | 11 500 | Model records and reimport (S9, S13)                  |
| R12 | Geometry, buffers and materials          | 9 700  | Cooked meshes and materials                           |
| R13 | Scenes and physics                       | 11 400 | Scene and physics sidecar cooks                       |
| R14 | Scripting, input and audio               | 5 700  | Script and input cooks                                |
| R10 | Textures                                 | 13 200 | Texture import and cooks                              |
| R17 | Test kits                                | 6 200  | The trust placed in every test above                  |

## R1. Serio IO

**Files.** `Serio/*.h`, `Serio/*.cpp`, `Serio/Detail/*`,
`Serio/Platform/Windows/*`, `Serio/Platform/Posix/*`; `Serio/README.md`,
`Serio/Docs/*.md`.

**Objectives.** The byte layer is correct, bounded and crash-safe, and its
API is the only way the engine reads, writes, hashes and publishes files.

**Critical points.**

- `AtomicFile` and `Platform/Windows/MoveFileOver.cpp`: a publish either
  leaves the old file or the complete new one, never a truncated file or a
  stray temporary sibling, including when the process dies between steps and
  when the target is open for reading by another process (the editor reads
  cooked roots while ImportTool writes them).
- `FileLock`: what it locks, which processes honour it, and what happens to a
  lock left by a killed process.
- `Reader`, `Record`, `ByteReader`: length prefixes are bounded before
  allocation (`kTooLarge`), and packed records move whole without
  `reinterpret_cast` views of unaligned bytes. The remaining casts in
  `Reader.h`, `Writer.h`, `ByteBuffer.h` and `ByteReader.h` are justified or
  removed.
- `Region`, `RegionReader`, `RegionStream`: no read escapes its region; the
  region of a region composes offsets correctly at the 64-bit limit.
- `Integrity`: hashing around skipped ranges hashes exactly the bytes the
  format says, in the same order the writer hashed them.
- `IoDriver`, `AsyncFile`, `Platform/Windows/IocpBackend.cpp`: a cancelled or
  timed-out read never completes into freed memory; every completion is
  delivered once; shutdown with reads in flight is clean.
- `MappedFile`: behaviour when the file shrinks or is replaced while mapped.
- Error mapping (`Error.cpp`, `SystemErrors.cpp`): every OS error becomes a
  typed Serio error; sharing violations and missing files are distinct.

## R2. Binary formats

**Files.** `Data/PakFormat*.h`, `Data/PakFormatSerioLoaders.h`,
`Data/PakFormatSerioWriters.h`, `Data/PakFormatVersions.{h,inc}`,
`Data/LooseCooked*`, `Data/ResourceDescriptor.*`, `Data/PakCatalog.*`,
`Data/PatchManifest.h`, `Data/CookedSource.*`, `Data/CookedOpenError.h`,
`Data/AssetReferences.*`, `Data/DescriptorStrings.*`, `Data/SourceKey.h`,
`Data/AssetType.h`; the engine readers' refusal paths, read for the format
checks only: `Content/PakFile.*`, `Content/LooseCookedIndex.*`,
`Content/Internal/LooseCookedIndex{Impl,Validation}.*`; their tests in
`Data/test` and `Content/test`; `Data/Docs/binary_packing_discipline.md`,
`Content/Docs/loose_cooked_content.md`, `Cooker/Docs/Pak/paktool_design.md`.

**Objectives.** The pak, loose index and sidecar formats are fully specified
by their headers and docs, so the managed readers of S7 can be written from
them alone, and every engine reader refuses what it cannot read.

**Critical points.**

- Every packed struct has `static_assert`s for size, alignment and the offset
  of each field the managed side reads; reserved bytes are written zero and
  checked on read.
- `PakFormatVersions.inc` is the one source of every format, asset and
  importer version; no header, tool or doc restates a number that can drift
  from it. A reader refuses any other version with a typed code; no reader
  of an older version survives.
- Pak layout: payloads and regions start on `kPakPayloadAlignment` (4096),
  regions are padded to it, tables, assets, directory, browse index and
  footer are aligned to 16; the footer's CRC field offset and the CRC range
  are what the reader checks.
- Resource rows carry a full SHA-256; a zero digest means "no digest" only
  where the format says so (loose data files), and nothing falls back to a
  truncated or non-SHA fingerprint.
- `ResourceDescriptor`: the single `.otex`/`.obuf` codec encodes and decodes
  both sidecars, each embedding its pak table row (`TextureResourceDesc`,
  `BufferResourceDesc`) unchanged, and round-trips every field.
- `PakCatalog`, `PatchManifest`: dependency records state requirements of
  other mounts (R10), not exact identities; tombstones and deletions are
  encoded once.
- String tables (`DescriptorStrings`): offsets and lengths are bounded and
  strings are valid UTF-8 where the format promises it.
- The `PakFormat_physics.h` and `PakFormat_input.h` `NOLINT` blocks (C arrays,
  macros) are needed by the layout, not a shortcut.
- `PakFormat_core.h:366` (compression enum TODO) and
  `PakFormat_geometry.h:174` (skeleton reference): confirm the reserved space
  lets these land without a format break, or record the break.

## R3. Asset model and descriptor readers

**Files.** `Data/SceneAsset.*`, `Data/SceneRecord*`, `Data/SceneComposition.*`,
`Data/SceneChangeValues.h`, `Data/SceneIds.h`, `Data/GeometryAsset.*`,
`Data/GeometryIndices.h`, `Data/MaterialAsset.*`, `Data/ScriptAsset.*`,
`Data/PhysicsSceneAsset.*`, `Data/Input*Asset.*`, `Data/*Resource.*`,
`Data/StableId.*`, `Data/Schema/*`, `Data/Tools/SceneSchemaCodeGen/*`,
`Data/LightValidation.h`, `Data/LodPolicyValidation.h`,
`Data/SurfaceInventory.*`, `Data/AssetRuntimeBindings.h`, `Data/Asset.h`,
`Data/DescriptorHash.*`,
`Data/ComponentType.h`, `Data/MaterialDescriptor.*`, `Data/MaterialDomain.h`,
`Data/MeshType.h`, `Core/Types/ShaderRequestRules.*`,
`Data/ShaderReference.*`, `Data/SourceOrigin.h`, `Data/SurfaceId.h`,
`Data/Vertex.h`, `Data/HalfFloat.h`, `Data/Unorm16.h`,
`Data/ToStringConverters.cpp`; `Data/Docs/physics.md`,
`Data/Docs/material_system.md`, `design/runtime-core/scene-composition.md`.

**Objectives.** Each asset class is a validating reader of untrusted bytes
and the in-memory model the interop hands to the editor.

**Critical points.**

- `SceneAsset`: node, component and script-slot counts and indices are
  bounded; component tables are checked for the sort order and type order the
  reader assumes; the header content hash is verified, not trusted;
  instances, changes and starting state by node path refuse dangling paths.
- `SceneSchemaCodeGen`: `SceneComponents.yaml` is the one source, validated
  against `SceneComponents.schema.json`; generation of the versioned
  `Generated.SceneComponents.h` and `Generated.SceneChanges.h` is
  deterministic, and stale checked-in headers cannot go unnoticed;
  `PropertyRange` limits match the schema.
- `GeometryAsset.h` (1 300 lines in a header): views are refused with typed
  errors, never aborts, when they draw nothing, leave their buffers or name
  vertices outside the view, including buffers bound after the load; 16- and
  32-bit indices are read without assuming alignment; the remaining vertex
  `reinterpret_cast` rests on the alignment the binding checks.
- `MaterialDescriptor`, `ShaderReference`: domains, flags, stages and
  shader references refuse unknown values with typed errors; shader paths and
  entry points follow the one shader request rule set.
- `StableId`, `SceneIds`: identity derivation is stable across platforms and
  documented, because the editor keys scene nodes by it.
- `PhysicsSceneAsset`: bindings name nodes of the scene they ship with, in
  the table order lookups rely on; wheel slices and soft body vertex lists
  stay within their tables and records. Anything else is refused.
- `Vertex.h:34` (skinned and morph TODO): confirm the vertex format reserves
  what that work needs, or record the break.

## R4. Mounts, precedence and dependency checks

**Files.** `Content/IAssetLoader.h`, `Content/AssetLoader.*`,
`Content/AssetLoaderLoading.cpp`, `Content/ContentMounts.h`,
`Content/DependencyRequirementError.*`, `Content/PakFile.*`,
`Content/LooseCookedIndex.*`, `Content/VirtualPath*`,
`Content/Internal/ContentSource*`, `Content/Internal/IContentSource.*`,
`Content/Internal/LooseCooked*`, `Content/Internal/PakFileSource.h`,
`Content/Internal/PatchResolutionPolicy.*`,
`Content/Internal/ContentIdentity*`, `Player/PlayerModule.cpp` and the
Player launch schema; `Content/Docs/patch_resolution_contract.md`,
`Content/Docs/virtual_paths.md`, `Content/Docs/loose_cooked_content.md`,
`design/runtime-core/scene-instantiation-and-player.md`.

**Objectives.** The mounted set behaves as its contract states under every
sequence the editor will drive: mount, prepare and commit, generation swap,
retire, clear, load during a swap.

**Critical points.**

- `PrepareMountsAsync` and `CommitPreparedMounts` are the only atomic
  replacement of the whole set: a refused preparation leaves the mounted
  layers and the current binding view unchanged; a commit is all or nothing.
- Precedence: newest mount wins, lowest precedence first in a prepared set;
  `ClearMounts` keeps the engine pak beneath everything; tombstones hide
  lower layers and nothing else.
- `DependencyRequirementError::Failures()`: every requirement failure kind
  (`kMissing`, `kTombstoned`, `kTypeMismatch`, `kMountMismatch`) is reachable,
  across loose roots and paks, and names the mount and the requirement.
- `MountLooseCookedGeneration` and `RetireLooseCookedGeneration`: a load in
  flight against a retired generation completes or cancels cleanly and never
  reads the new generation's half-written files.
- `ContentSourceRegistry`: source-key conflicts, `PruneExpiredSources`, and
  the binding-view lifetime (`CaptureView`) under concurrent loads.
- Verified pak mounts (`AssetLoader`): the CRC pass awaits its reads on the
  loop, hashes the handle the mount then maps, and refuses a corrupt or
  unverifiable pak with a typed code.
- `VirtualPath` and `VirtualPathResolver`: one grammar, shared with the
  managed side through `VirtualPathCases.json`; the two `reinterpret_cast`s
  in `VirtualPath.cpp` are safe for every UTF-8 input.
- Open failures of paks and loose indices are typed (`CookedOpenError`), not
  logged and swallowed.
- `PlayerModule.cpp`: `DependencyRequirementError` at launch becomes
  `player.launch.dependency_missing`, and the launch file is
  validated against its installed schema.
- `AssetLoader.cpp` (3 300 lines) and `AssetLoader.h` (1 700 lines): flag
  responsibilities that belong in the internal services already split out.

## R5. Loading, residency and instantiation

**Files.** `Content/Loaders/*`, `Content/Internal/ResourceLoadPipeline.*`,
`Content/Internal/InFlight*`, `Content/Internal/DependencyCollector.h`,
`Content/Internal/Eviction*`, `Content/Internal/ContentRelease*`,
`Content/Internal/PayloadBudget.*`, `Content/Residency*`,
`Content/ContentLoadScope.h`, `Content/Internal/ContentLoadScopeState.h`,
`Content/Internal/ContentBindingBundle.*`,
`Content/Internal/ContentPublication.h`, `Content/Internal/ResourceRef.h`,
`Content/Internal/*QueryService.*`, `Content/Internal/PhysicsBindings.h`,
`Content/*InputContextHydration.*`, `Content/AssetValidation.*`,
`Content/ContentId.h`, `Content/EvictionEvents.h`,
`Content/Internal/ResourceTypes.h`, `Content/Loader*.h`,
`Content/OperationCancelledException.h`, `Content/ResourceKey.*`,
`Content/ResourceTable.h`, `Content/ResourceTypeList.h`,
`SceneInstantiation/*`; `Content/Docs/eviction_and_residency.md`,
`deps_and_cache.md`, `hot_reload.md`, `truly-async-asset-loader.md`,
`scenes_and_levels.md`.

**Objectives.** Loads are correct and safe under concurrency, cancellation,
eviction and hot reload, which Live Preview exercises constantly.

**Critical points.**

- In-flight deduplication: two loads of one key share one operation; a
  cancelled waiter does not cancel the others; a failed load is not cached as
  success.
- Each loader validates descriptor references against the resource tables
  (index in range, kind matches) before use, and loose data files are
  verified per resource from their table rows (S6d).
- Eviction ordering: a resource is not evicted while a dependent asset holds
  it; dependency cycles terminate; `ResidencyPin` and `PayloadBudget`
  accounting stay balanced under failure paths.
- `ContentReleaseQueue`: GPU-side release is deferred to a safe frame, never
  run on a loader thread.
- `SceneInstantiation`: instances, changes and starting state apply in the
  documented order; a bad reference fails the instantiation with a code
  rather than producing a partial scene; `EnvironmentHydration` refuses
  unknown blocks.
- `MaterialLoader.h:314` (shader references are not resources): confirm the
  effect on hot reload and dependency tracking.
- Docs: `implementation_plan.md` and the other plan-style docs in
  `Content/Docs` are current or retired.

## R6. Cooked roots: write, read, verify

**Files.** `Cooker/Cooked/*` (`CookedContent`, `CookerStorage`, `Validate`),
`Cooker/CookedVersions.*`, `Cooker/Import/Internal/LooseCooked*`
(`LooseCookedWriter`, `LooseCookedLayout`, `LooseCookedIndexRegistry`),
`Cooker/Import/Internal/ResourceTable*`, `Cooker/Import/Internal/Emitters/*`,
`Cooker/Import/Internal/FileSourceReader.*`,
`Cooker/Import/Internal/SourceLayoutHash.h`; `design/proposals/cooked-sources/`.

**Objectives.** A loose cooked root is written so that a reader never sees an
inconsistent state, and `CookedContent` tells the editor exactly what a
cooked source provides, deletes and requires.

**Critical points.**

- Publication order: data files before the index, the index as the commit
  point, through `CookerStorage` publication only; an interrupted cook leaves
  the previous root readable.
- Two cooks writing one root: `StorageLock` and `LooseCookedIndexRegistry`
  serialize them, or the refusal is typed.
- `ResourceTableAggregator` deduplication uses the full SHA-256 and the
  descriptor bytes, and two different payloads never share a row.
- Emitters compute each resource's digest from the bytes they write; no pass
  rereads a data file to hash it.
- `CookedContent::Open` and `IsCookedSource` identify a cooked source by its
  content, not by a file name; `CookedSummary` (provides, deletes, requires)
  matches what the runtime mounts.
- `VerifyStorage`, `CheckContent`, `ValidateContent`, `Validate`: every
  corruption a reader would hit (row out of range, digest mismatch, missing
  data file, version) is reported with its code before mount.
- `CookedVersions`: one place states every cooked format and importer
  revision; a change of an importer's output bumps its revision.

## R7. Source mounts, references and model records

**Files.** `Cooker/Import/SourceMounts.*`, `Cooker/Import/ModelImport*`
(`ModelImportRecord`, `ModelImportState`), `Cooker/Import/Naming.*`,
`Cooker/Import/Internal/AssetReferenceBuilder.*`,
`Cooker/Import/Internal/Utils/MountedCookedSources.*`,
`Cooker/Import/Internal/Utils/TextureReferenceResolver.*`,
`Cooker/Import/Internal/Utils/VirtualPathResolution.h`,
`Cooker/Import/Internal/SidecarSceneResolver.*`, the `reference`, `mounts`,
`model-import` and `name` schemas in `Cooker/Import/Schemas`;
`design/proposals/source-mounts/README.md`,
`design/proposals/portable-references/design.md`,
`Cooker/Tools/ImportTool/README.md` (records).

**Objectives.** The editor's project model (S8 to S13) is built on these
rules; each must have one implementation, match its schema, and refuse
everything outside the rule.

**Critical points.**

- `LocateReference`: the anchor is the referrer's parent folder for every
  format; `./` and `../` are resolved once; a reference that climbs out of
  its mount is refused with `import.reference_outside_mount` (one code).
  Windows-specific inputs are covered: case, drive-relative and UNC paths,
  `\` separators, trailing dots and spaces, junctions and symbolic links.
- Mount names: `IsValidMountName`, `oxygen.mounts.schema.json` and
  `VirtualPath` agree exactly (one rule); conflicts and folder overlaps are
  refused, cooked folders included.
- Mount kinds: `Source` and `Cooked` only; a `Cooked` mount accepts a loose
  root or a pak; an unknown kind is refused, never read as `Source`.
- `ModelImportRecord`: a record lives at its model's identity, names its
  source relative to itself, and `Read`, `MakeSettings`, `SelectedGeneration`,
  `LocateRecord` and `Prepare` refuse malformed records with codes; no
  `Imports/` path, `virtual_path` field or content-root input survives.
- `AssetReferenceBuilder`: products spell references the way
  `LocateReference` resolves them (round trip), relative within a mount and
  absolute across mounts.
- `MountedCookedSources` and `TextureReferenceResolver` resolve through the
  one resolver; flag any second lookup path.
- `Naming`: product names derived from sources are stable across platforms
  and reimports.

## R8. Manifests, requests and source analysis

**Files.** `Cooker/Import/ImportManifest.*`, `Cooker/Import/ImportOptions.*`,
`Cooker/Import/ImportRequest.*`, `Cooker/Import/ImportSource*`,
`Cooker/Import/CapturedInputSet.*`, `Cooker/Import/EngineContent.*`,
`Cooker/Import/*RequestBuilder.h`, `Cooker/Import/*Settings.h`,
`Cooker/Import/BufferImportTypes.h`, `Cooker/Import/FileInfo.h`,
`Cooker/Import/SceneSourceInspection.*`,
`Cooker/Import/Internal/*RequestBuilder.*`,
`Cooker/Import/Internal/SourceAnalysisRunner.h`,
`Cooker/Import/Internal/Utils/{ImportSettingsUtils,JsonSchemaValidation,DescriptorDocument,BufferViews}.*`,
the other schemas in `Cooker/Import/Schemas`;
`Cooker/Docs/Import/async_import_pipeline_v2.md`.

**Objectives.** Every file the editor writes for the cooker (manifests,
descriptors, settings) is validated the same way, and source analysis gives
the editor complete, located diagnostics without cooking.

**Critical points.**

- Schema and code parity: each rule in a schema is enforced by the request
  builder or by schema validation before it, and each code-only rule is
  either in the schema or documented as semantic. Build a table per schema.
- `ImportManifest.cpp` (2 400 lines): unknown keys, duplicate job IDs,
  `depends_on` cycles and missing dependencies are refused; defaults are
  stated once.
- `ImportOptions`: no `content_hashing` option or manifest key remains; every
  option has a manifest key and a CLI flag or is documented as API-only.
- Request builders: one per descriptor family, each normalizing the document
  the same way (references, names, defaults); the normalized JSON they hash
  for change detection is canonical (key order, numbers).
- `ImportSourceAnalysis.cpp`: every diagnostic carries `source_path` and,
  where applicable, `object_path`; analysis and cook report the same code for
  the same defect.
- `EngineContent`: the engine pak build uses the same manifests and rules as
  any project; the built-in material reference is relative or explicitly
  cross-mount.
- Optional inputs missing: the import warns and continues; only required
  inputs fail.

## R9. Import execution

**Files.** `Cooker/Import/AsyncImportService.*`, `Cooker/Import/Importer.*`,
`Cooker/Import/ImportConcurrency.h`, `ImportProgress.h`, `ImportReport.h`,
`ImportDiagnostics.h`, `ImportJobId.h`, `IAsyncFileReader.h`,
`Cooker/Import/Internal/{AsyncImporter,ImportEventLoop,ImportJob,ImportJobParams,JobEntry,ImportPipeline,ImportPlanner,ImportSession*,ImportSourceSnapshot,WorkDispatcher,WorkPayloadStore}.*`;
`Cooker/Docs/Import/async_import_pipeline_v2.md`, `async_import_planner.md`,
`async_file_io.md`.

**Objectives.** Imports run concurrently, cancel cleanly, report progress in
order, and produce the same output whatever the concurrency.

**Critical points.**

- Determinism: the same inputs give byte-identical cooked output with one
  worker or many; table row order does not depend on completion order.
- Cancellation: `CancelJob` (now `[[nodiscard]]`) cancels at every stage,
  leaves no partial publication, and reports a cancelled outcome distinct
  from failure.
- `WorkDispatcher.cpp` (2 000 lines): no deadlock when every worker waits on
  a dependency; back-pressure bounds memory in `WorkPayloadStore`.
- `ImportSourceSnapshot`: a source changed during an import is detected, not
  half-read.
- `ImportReport`: diagnostics, products and telemetry are complete for
  failed and cancelled jobs, and progress events end with exactly one final
  event per job.
- Session reuse: state from one batch never leaks into the next.

## R10. Textures

**Files.** `Cooker/Import/Texture*`, `Cooker/Import/ScratchImage.*`,
`Cooker/Import/Internal/TextureCooker.*`, `Cooker/Import/Internal/Image*`,
`Cooker/Import/Internal/TextureSourceAssembly_internal.h`,
`Cooker/Import/Internal/Pipelines/TexturePipeline.*`,
`Cooker/Import/Internal/Jobs/Texture*`, `Cooker/Import/Internal/bc7/Bc7Encoder.*`;
`Cooker/Docs/Import/texture_work_pipeline_v2.md`.

**Objectives.** Every texture kind the engine renders can be imported, and
the cooked bytes match what the GPU upload path expects.

**Critical points.**

- Decode of untrusted images (`ImageDecode.cpp`): dimensions, channel
  counts and sizes are bounded before allocation; the six `NOLINT`s and the
  `reinterpret_cast`s in `ImageDecode.cpp` and `ImageProcessing.cpp` (18) are
  safe for odd widths and unaligned rows.
- Color space: sRGB and linear are tracked from source to format; mip
  filtering is done in linear space; normal maps are renormalized.
- `ImageProcessing.cpp:654`: 3D textures get no depth-downsampled mip chain.
  Confirm whether volume textures are importable; if they are, this is a
  defect.
- Cube maps, arrays and packed channels (`TexturePackingPolicy`) produce the
  layouts the loader and Vortex upload expect, including row pitch and the
  512-byte staging placement.
- BC7 and other block formats: block alignment for non-multiple-of-4 sizes.
- The `.otex` sidecar and the texture row's digest are computed from the
  stored bytes.
- `TextureCooker.cpp:615`: an unused descriptor parameter; remove or use.

## R11. Model adapters and node identity

**Files.** `Cooker/Import/Internal/gltf/Gltf*` (adapter and meshopt
wrapper), `Cooker/Import/Internal/fbx/{FbxAdapter.*,CoordTransform.h,FbxMaterialTextures.h}`,
`Cooker/Import/Internal/AdapterTypes.h`, `Cooker/Import/Internal/Model*`
(`ModelNodeIdentity`, `ModelSettings`, model sources),
`Cooker/Import/Internal/NodePathTable.h`,
`Cooker/Import/Internal/DroppedSourceFeatures.*`,
`Cooker/Import/Internal/ImportedLightSemantics.h`,
`Cooker/Import/Internal/ImportedPropertyCommit.h`,
`Cooker/Import/Internal/Jobs/{Glb,Fbx}*`, `Cooker/Import/SurfaceProvenance.*`,
`Cooker/Import/SceneImportSettings.*`.

**Objectives.** A reimport of a changed source keeps every identity the
editor has stored (nodes, surfaces, materials), and both adapters give the
same engine model for the same scene.

**Critical points.**

- `ModelNodeIdentity` and `NodePathTable`: node identities survive reorder,
  rename of siblings and insertion; duplicate names get stable,
  documented disambiguation.
- `material_remap` and `SurfaceProvenance`: remaps are relative or explicit
  cross-mount references and survive reimport.
- glTF and FBX parity: units, handedness, up axis (`CoordTransform.h`),
  pivots, negative scales and light units agree; a shared scenario test
  compares them.
- `DroppedSourceFeatures`: every feature not imported is reported, not
  silently dropped.
- `FbxAdapter.cpp` (3 800 lines) and `GltfAdapter.cpp` (3 400 lines): source
  data is bounds-checked before use, since `ufbx` and `cgltf` hand back
  file-controlled indices.
- The glTF and FBX importer revisions (`PakFormatVersions.inc`) are bumped by every output change.

## R12. Geometry, buffers and materials

**Files.** `Cooker/Import/Internal/{Geometry*,Mesh*,LodGeneration.*,SurfaceAllocation.*,Material*,Buffer*}`,
`Cooker/Import/Internal/Pipelines/{Geometry*,MeshBuild*,Material*,Buffer*}`,
`Cooker/Import/Internal/Jobs/{Geometry*,Material*,Buffer*}`;
`Cooker/Docs/Import/geometry_work_pipeline_v2.md`,
`material_work_pipeline_v2.md`, `buffer_work_pipeline_v2.md`.

**Objectives.** Cooked meshes, buffers and materials are correct, compact and
reproducible, and every material property the engine renders is authorable.

**Critical points.**

- `GeometryPipeline_tangents`: MikkTSpace tangents match the renderer's
  convention, including mirrored UVs and degenerate triangles.
- `MeshBuildPipeline.cpp` (2 200 lines), `MeshOptimization`, `LodGeneration`:
  vertex and index limits pick the right index width; LOD policies validate;
  optimization is deterministic.
- `BufferSource` JSON input: product names are relative; digests come from
  the bytes stored.
- `MaterialPipeline.h:60`: `header.streaming_priority` and
  `header.variant_flags` are not wired from import settings. The V0.1 rule is
  that every engine capability is authorable, so these are gaps to close or
  record.
- `MaterialPipeline.cpp:199` (single UV set) and `:281` (alpha cutoff not
  clamped): confirm the behaviour on sources with several UV sets and with an
  out-of-range cutoff.

## R13. Scenes and physics

**Files.** `Cooker/Import/Internal/Scene*` (`SceneSource`, `SceneBuild`,
`SceneNodeImportDefaults`), `Cooker/Import/Internal/NodeReferenceJson.h`,
`Cooker/Import/Internal/Pipelines/{Scene*,Physics*,CollisionShape*}`,
`Cooker/Import/Internal/Jobs/{Scene*,Physics*,CollisionShape*}`;
`Cooker/Docs/Import/scene_work_pipeline_v2.md`, `Data/Docs/physics.md`.

**Objectives.** Scene descriptors and their physics sidecars cook to exactly
what the runtime instantiates, and a recook of one invalidates the other
when it must.

**Critical points.**

- `PhysicsSidecarImportPipeline.cpp` is 3 900 lines: review its structure
  first and flag functions that mix parsing, validation and emission.
- Sidecar binding: a sidecar names its scene's key, node count and SHA-256
  and the cook's logical scene reference; a scene recook that changes the
  nodes refuses or recooks the stale sidecar (`PhysicsSidecarRecook`).
- Node references in descriptors resolve by node path once; unknown paths
  are refused with the path in the diagnostic.
- Collision shapes and physics materials: identities come from the file
  path (no `virtual_path`, no `identity_conflict`); shape parameters are
  range-checked.
- `SceneDescriptorImportJob` cooks into the same layout `SceneBuilder` (R17)
  produces; a test compares them.

## R14. Scripting, input and audio

**Files.** `Cooker/Import/Internal/Pipelines/{Script*,InputImport*}`,
`Cooker/Import/Internal/Jobs/{Script*,Input*,Audio*}`;
`Cooker/Docs/Import/scripting_pipelines.md`.

**Objectives.** Script assets, scripting sidecars and input contexts cook
with typed parameters and resolvable references, and audio's status is
explicit.

**Critical points.**

- `ScriptingSidecarImportPipeline.cpp` (2 300 lines): script slots bind by
  node path; parameter types and defaults are checked against the script's
  declared parameters; the script cache version is bumped with its
  format.
- Input references end with `.oiact`; local action names and relative
  references are unambiguous (`InputImportPipeline.cpp`).
- `AudioImportJob.cpp`: five TODOs, no read, no pipeline, no emission.
  Establish whether an audio job is reachable from a manifest or the CLI; if
  it is, it must refuse with a code instead of reporting success.

## R15. Pak build and pak tools

**Files.** `Cooker/Pak/**` (`PakBuilder`, `PakPlanBuilder`, `PakPlanPolicy`,
`PakValidation`, `PakWriter`, `PakCatalogIo`, `PakManifestWriter`,
`PakMeasureStore`, `Internal/PakSourceDevices`, the catalog schema),
`Cooker/Tools/PakTool/*` (with `ScriptSealing`, `StagedSourceStorage`,
`ArtifactPublication` and the build report schema), `Cooker/Tools/PakDump/*`;
`Cooker/Tools/PakTool/README.md`, `Cooker/Docs/Pak/paktool_design.md`,
`paktool_impl_plan.md`, `design/content-pipeline/pak-builder-api-specification.md`.

**Objectives.** Paks are built deterministically from cooked sources,
validate before they are written, publish atomically, and patch against a
base correctly.

**Critical points.**

- `PakPlanBuilder.cpp` (3 000 lines): the plan states every offset and size;
  `PakValidation` checks alignment, overlap, table sizes and the CRC field,
  and `PakWriter` refuses a plan that contradicts itself
  (`pak.write.*` codes).
- Determinism: the same inputs give a byte-identical pak.
- Patches: base and dependency catalogs, the `identity_conflict` codes in
  `PakPlanBuilder.cpp` (dependency and base catalogs), deletions and
  tombstones.
- Requirements: the catalog records requirements of other mounts, and the
  runtime check (R4) reads them the same way.
- `ScriptSealing.cpp`: what it seals and how a sealed script is verified at
  load.
- `ArtifactPublication` and `StagedSourceStorage`: the pak and its catalog
  publish together or not at all.
- The browse index is mandatory (no `embed_browse_index`).
- `PakDump`: dumpers read through the product readers, not their own
  layouts.

## R16. Tool command lines and SDK contracts

**Files.** `Cooker/Tools/ImportTool/*` (with `BatchCommand`, `ReportJson`,
`SourceAnalysisCommand`, `SourceMountOptions`, `ImportRunner`, `ServiceConfig`,
`StopSignal`, `ReclaimCommand`, `RemoveCookedCommand` and the `UI/` screens),
`Cooker/Tools/Inspector/*` (with `InventoryReport`, `DependencyReport`,
`CookedRootCommand` and its schemas), `Cooker/Tools/Common/*`; the schema
install rules in `cmake/Install.cmake` and the `Cooker`, `Data`, `Core`,
`Player`, `PakTool` and `Inspector` `CMakeLists.txt`; the ImportTool,
Inspector and Common READMEs, `cmake/SDK_README.md`.

**Objectives.** The editor's process boundary is a stable, versioned,
schema-validated contract: arguments, exit codes, JSON reports and the
installed schemas.

**Critical points.**

- Every JSON output (import report, source analysis, inventory, dependency
  report, pak build report) is validated against its installed schema in a
  test, and every schema has a version the producer writes.
- Exit codes: success, refusal, cancellation and internal error are
  distinct and documented; a refusal always has diagnostics in the report.
- `StopSignal`: Ctrl+C or a parent's termination leaves no partial
  publication and still writes the report.
- Batch mode with `--no-tui` writes nothing interactive; stdout and stderr
  carry what the README says.
- Paths in reports are spelled consistently (absolute file paths, identities
  as virtual paths), so the editor can map diagnostics back to its files.
- `BatchCommand.cpp` (2 000 lines): flags that duplicate manifest keys follow
  one precedence rule.
- The SDK installs every schema the editor validates against and nothing it
  must not depend on (engine build isolation).

## R17. Test kits

**Files.** `Cooker/testing/*` (`SceneBuilder`, `PhysicsSidecarBuilder`,
`DescriptorDefects`, `ModelRecord`, `GeometryBuilder`, `MaterialBuilder`,
`ScriptBuilder`, `LooseCookedRoot`, `CookedPak`, `PackedPak`, `PakWorkspace`,
`Materialize`, `MemoryFileSystem`, `CookedBytes`, `ProductLoad`, `Seeds`),
`Serio/Testing/*`; `design/proposals/cooked-content-builders/README.md`.

**Objectives.** The tests of every set above can be trusted: the kit builds
artifacts with the product encoders and fails when the product readers
refuse them.

**Critical points.**

- Builders encode through the product writers and never restate a layout;
  `Bytes()` fails when the product reader refuses the result.
- `DescriptorDefects`: each named defect produces exactly one defect, and
  every reader refusal code has a defect that triggers it.
- `MemoryFileSystem`: replace, rename, sharing and case behaviour match the
  real storage port closely enough that atomic publication tests mean
  something; differences are documented.
- `Materialize`: a materialized loose root is the same as a cooked one (index
  version, digests, layout).
- The cooked script, collision shape, physics material and input descriptors
  that S6f moved onto the kit are covered by builders with hardening tests.
