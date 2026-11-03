# Loose cooked content (filesystem-backed cooked source)

Read [published generations](#published-generations) for lifetime and
replacement, [complete integrity inventory](#complete-integrity-inventory) for
admission and validation.

This document specifies how Oxygen Content loads **cooked Oxygen runtime
formats** from a **directory of loose cooked files** (“loose cooked”), in
addition to `.pak` containers.

The goal is a single content model with two container forms:

- **Loose cooked** for fast editor iteration (Play-in-Editor).
- **PAK** for shipping and reproducible builds.

Related:

- Conceptual architecture and async pipeline: `truly-async-asset-loader.md` and
  `overview.md`
- Dependency tracking and cache semantics: `deps_and_cache.md`
- [Published generations](#published-generations)

## Published generations

`AssetLoader::MountLooseCookedGeneration(root, replaces)` mounts an immutable
generation, optionally replacing one active generation at the same source
priority. It acquires the existing `.generation.lock` shared before reading the
index. A new generation has a fresh cooked SourceKey and runtime source ID;
reusing another generation's identity is rejected before changing the mount.
`RetireLooseCookedGeneration(source_key)` removes it from ordinary winning
lookup, unless a pak above it requires what only the generation provides
(`DependencyRequirementError`; it stays mounted). Loads in flight complete from
the retired generation. Neither operation clears unrelated assets, resources or
IBL caches.
`AddLooseCookedRoot` dispatches roots carrying this marker to the same
lease-aware mount operation.

Active sources have registry ownership. Retired sources have weak registry
entries; decode operations and loaded asset/resource owners retain their actual
source and shared marker lock. Old asset objects expose their exact
SourceOrigin. Source-qualified operations select that source exactly. A new
logical load and its AssetKey dependencies resolve through one immutable view of
the declared layers, with later layers winning and the tombstones of paks
masking lower definitions; a texture another source holds, named by key,
resolves through the same view and is recorded with the asset. Physical
resource indices always address the container of the selected asset. Contextual reads and repeated publication preserve the object's
recorded bindings; activating another layer never silently rebinds an existing
object.

Cache reuse must match both the selected descriptor and its binding view. An
unchanged base scene must acquire patched dependencies on a new load, while a
held earlier scene keeps its old dependencies. Async graph loading retains its
view and source owners through publication; mount changes cannot mix views
inside a graph. Physical resource cache identities remain source-local and
reusable.

Dependency graph nodes are the existing source-qualified asset cache identities,
not bare AssetKeys. Old and new generations can therefore coexist, release and
trim independently. Source instances and opaque content IDs are not recycled. A
reopened generation uses an independent opening, allowing retired metadata to
expire while the new instance remains mounted.

For complete context changes, `IAssetLoader::PrepareMountsAsync` owns the
candidate mounts (each a `ContentMount`: a loose cooked root or a pak file,
lowest precedence first) and reads source metadata on the worker pool. The
engine content pak stays beneath them, and pak layering rules apply as for
`AddPakFile`. Its move-only `PreparedMountSet` binds the originating loader
lifetime, restart epoch and mount revision. A stopped loader or changed mount
set invalidates the candidate.

`CommitPreparedMounts` requires drained loads and prepares cache retirements
before changing active precedence. Validation/allocation failure leaves the old
roots selected. The source registry switches without invoking observers and
returns a `MountRetirement` owner. A caller with a `VirtualPathResolver` swaps
its prepared resolver before finishing that retirement. Observer reloads
therefore see one complete new context; late checkouts cannot debit replacement
entries. Canceled subscriptions are skipped, and loader destruction ends
delivery safely. Expired source/locator metadata is pruned during later
preparation, outside the swap. Source-qualified objects retain their original
generation leases.

Content does not delete generation directories. A publisher can reclaim only an
unselected generation after acquiring its existing marker exclusively. Loaded
objects, cached objects and in-flight operations are all holders. The operating
system lock also protects against reclamation by another process. Publisher CAS
and selection rules are separate from this byte-lifetime contract; the shared
lock primitive is owned by [Serio](../../Serio/Docs/publication.md#file-locks).

A plain loose root is published by commits
([loose root publication](../../../../design/proposals/cooked-sources/root-publication.md)):
its index is the only commit, every other file but a data file has a content
name, and a data file's committed length is the size its index records. A
writer holds `container.writer.lock` for its whole session, so a root has one
writer at a time. A mounted plain root holds the reader lease of the commit it
mapped, `.leases/<commit>.lock`, shared, for the source's life
(`internal::LoadLeasedIndex`): no writer reclaims a file that commit lists
while the lease is held, and each data file is read up to its committed
length, whatever a writer appends after it. A generation is held by its own
lease instead.

A loose root states what its assets require of other mounts. It mounts with
the requirements the layers below it do not meet, which
`IAssetLoader::UnmetRequirements` lists and the mount logs: a load of what one
names fails as any missing reference does. A pak's unmet requirement refuses
its mount.

---

## Scope and constraints

- No runtime import of source formats (FBX/GLTF/PNG/etc.). The editor must cook
  to runtime formats first.
- Scenes can be handled in two editor workflows:
  - **Live-edited scenes** (not cooked): scenes reference assets by **virtual
    path** and the editor resolves them to `data::AssetKey`.
  - **Cooked scene descriptors** (optional): scenes are emitted as cooked
    descriptors (`AssetType::kScene`) and can be loaded from loose cooked roots
    or `.pak` like any other asset.
- Runtime loads assets by `data::AssetKey` and resources by `ResourceKey`.
- Editor responsibilities bridge **virtual paths** to runtime identifiers.

---

## Goals

- Provide a filesystem-backed cooked source that behaves like a container from
  the loader’s point of view.
- Keep loader decode logic container-agnostic (loaders do not care whether bytes
  come from PAK or a directory).
- Preserve identity and caching semantics:
  - `data::AssetKey` is the canonical asset identifier.
  - `ResourceKey` uniquely identifies a resource by
    `(source_id, resource_type, resource_index)`.
- Enable deterministic editor workflows: stable identity, stable resolution,
  actionable diagnostics.

## Non-goals

- Runtime import of source formats.
- In-place reload: a recooked root is mounted as a new
  [published generation](#published-generations).
- GPU uploads/residency/budgets.

---

## Key terms

- **Virtual path**: editor-facing identity persisted by scenes, e.g.
  `/Content/Materials/DarkWood.mat`.
- **Cooked root**: a directory containing cooked artifacts (descriptors,
  resource tables/data, and an index).
- **Content source**: runtime-facing provider of cooked bytes registered with
  `AssetLoader` (PAK or loose cooked).
- **Descriptor**: per-asset cooked bytes consumed via
  `LoaderContext::desc_reader`.
- **Resource tables/data**: cooked resource lookup tables and bulk payload
  streams consumed via `LoaderContext`.

---

## Core invariants

1. **Resource indices are source-local; logical references follow layer order**

   Table indices address their owning cooked source. New AssetKey dependencies
   select the effective winner in the load's layer view. Frozen bindings record
   exact selected objects and retain their sources, so release, contextual reads
   and trim cannot substitute a newer generation.

2. **Runtime identity includes the source opening**

   ResourceKey is an opaque loader-owned ID. Full identity equality includes the
   source instance, resource kind and table index. PAK, loose and synthetic
   resources share one ID namespace without encoding source ranges in the key.
   [Identity ownership](deps_and_cache.md#identities) defines lazy registration
   and metadata reclamation.

3. **Async loader contract applies equally to loose cooked**

   The loader is async-first:

   - Payloads are awaited through the loader's IO driver, a loop driver or,
     without a loop, an inline one; worker threads only decode.
   - Only the owning thread may mutate loader state (mounts, cache publish,
     dependency graph mutation, callbacks).
   - Loader decode must not call back into `AssetLoader` (dependencies are
     recorded via a collector).

---

## Runtime architecture

### Content sources

`AssetLoader` mounts cooked content sources and resolves assets by
`data::AssetKey`.

- `.pak` containers are adapted via an internal PAK-backed content source.
- Loose cooked roots are adapted via an internal directory-backed content
  source.

Both provide:

- Asset discovery by `data::AssetKey`.
- Readers for descriptors (`desc_reader`).
- Resource tables (descriptor rows) and payload regions, opened at mount.

### Mounting and resolution order

- Runtime mounting API:
  - `AssetLoader::AddPakFile(path)`
  - `AssetLoader::AddLooseCookedRoot(path)`
- Mount operations are owning-thread only.
- Asset resolution is **last-mounted-wins** in source registration order.

This ordering is editor policy: if multiple mounted sources overlap (engine
content, project content, overrides), the editor must register them
deterministically.

### Virtual paths are resolved above the loader

Runtime Content APIs are keyed by `AssetKey`/`ResourceKey` and do not interpret
virtual paths.

For editor/tooling workflows, `VirtualPathResolver` exists as a helper:

- It maps canonical virtual paths to `data::AssetKey` using mounted cooked
  indexes and PAK browse indexes.
- It uses **last-mounted-wins** in registration order.
- It logs a `WARNING` when the same virtual path exists in multiple mounts but
  maps to different `AssetKey`s.

---

## Loose cooked on-disk layout

Loose cooked content replicates the conceptual structure of a PAK, split across
files.

Directory layout:

```text
CookedRoot/
  container.index.bin
  assets/
    Textures/
      Cool.tex
    Materials/
      DarkWood.mat
  resources/
    buffers.table
    buffers.data
    textures.table
    textures.data
```

### `assets/**` (cooked descriptors)

Files under `assets/` are cooked descriptor files, not source assets.

- `assets/Textures/Cool.tex` is a cooked texture descriptor (not a `.png`).
- `assets/Materials/DarkWood.mat` is a cooked material descriptor.

These are what `LoaderContext::desc_reader` reads.

### `resources/*.table` and `resources/*.data`

- `*.table` files provide resource descriptors/rows.
- `*.data` files provide bulk payload byte streams.

Offsets stored inside cooked descriptors are interpreted relative to the
`AnyReader` stream opened by the mounted source for the relevant `*.data` file.

### `container.index.bin` (required)

The cooked index is required for loose cooked roots. It provides:

- `AssetKey -> descriptor relative path`.
- Optional virtual path mapping (`VirtualPath <-> AssetKey`) when enabled by
  index header flags.
- Resource file metadata (names, sizes, SHA-256 digests). The resource data
  files (`buffers.data`, `textures.data`, `scripts.data`, `physics.data`)
  record an all-zero digest; see
  [Complete integrity inventory](#complete-integrity-inventory).

Mount-time expectations:

- The index must be present, parseable, and versioned.
- Index header flags must be self-consistent.
- Section layout must be valid (no overlaps, no out-of-range spans).
- Referenced descriptor and resource files must exist.
- Normal mounts validate inventory membership, sizes and bounds. Full
  verification is opt-in at runtime and required for cook/publication
  validation.
- The encoder and the loader refuse a non-zero digest for a resource data file.
- All paths recorded in the index must be canonical:
  - container-relative, `/` separators
  - no `..`, no `\\`, no `//`
  - virtual paths start with `/` and contain no dot segments
- Duplicate records are rejected (e.g. duplicate `AssetKey`, duplicate
  virtual-path strings).

---

## Override/overlap policy and collision diagnostics

Overlaps are expected (engine content + project content + user overrides), but
must remain deterministic.

Rules:

- `AssetLoader` resolves `AssetKey` using last-mounted-wins in source
  registration order, with mount-level tombstone terminal masking.
- `VirtualPathResolver` resolves `VirtualPath -> AssetKey` using the same
  last-mounted-wins + tombstone policy and logs collisions when the same virtual
  path maps to different `AssetKey`s.

Strictness policy is editor-owned:

- Editor/PIE: allow overrides but warn deterministically.
- CI/shipping validation: treat selected collision cases as errors.

---

## Editor requirements (Play-in-Editor with loose cooked)

This section merges the editor capabilities required to use loose cooked roots
as a runtime content source.

### 1) Mount points and namespace management

- Maintain a mount registry mapping `virtual_root -> physical_root` for
  authoring.
- Maintain a runtime mount list of cooked sources to register at PIE startup:
  - loose cooked roots (directories)
  - `.pak` containers when desired
- Normalize physical roots to avoid duplicates (absolute/canonical/normalized
  separators).
- Support multiple sources under one virtual root (engine + project +
  overrides).

### 2) Canonical virtual path rules

- Validate virtual paths with the rules of [virtual paths](virtual_paths.md),
  which also state the case policy: identity is case-sensitive and case-only
  siblings are refused.

### 3) Cook pipeline must produce a valid loose cooked root

- Provide a Cook action that writes a deterministic cooked root containing:
  - `container.index.bin`
  - cooked descriptors under `assets/**`
  - cooked resource tables/data under `resources/*.table` and `resources/*.data`
- Ensure directory layout and naming are stable and mirror the virtual-path
  hierarchy for descriptors.
- Support incremental cook (regenerate only impacted outputs).
- Support atomic cook updates (stage then swap; avoid partial roots).
- Ensure internal container consistency:
  - descriptors reference valid rows in the corresponding resource tables
  - resource data offsets are valid for the referenced `*.data` files

### 4) Deterministic identity assignment and lookup

- Assign `AssetKey` deterministically from virtual identity (per the project’s
  canonical hashing scheme).
- Persist or regenerate mappings via `container.index.bin` so they are
  authoritative and reproducible.
- Provide lookup in both directions:
  - `VirtualPath -> AssetKey`
  - `AssetKey -> VirtualPath`

### 5) Build and validate `container.index.bin`

- Write a versioned index with required mappings and metadata:
  - `AssetKey -> descriptor relative path`
  - virtual-path mapping as enabled by header flags
  - resource file names and metadata
  - per-file size metadata and SHA-256 digests (none for resource data files)
- Enforce index correctness at cook time (fail early):
  - header flags consistent with included sections
  - no overlapping/invalid section ranges
  - no duplicate records
  - reject unknown/unsupported file-kind values
  - referenced paths are container-relative and canonical
- Provide an editor-facing validation command that:
  - verifies referenced files exist
  - verifies recorded sizes
  - verifies every indexed digest during full integrity validation, and each
    resource of a data file against its table row's SHA-256
  - reports actionable diagnostics

### 6) Runtime provisioning for PIE

- Register cooked sources with the runtime asset loader at PIE startup.
- Register sources in a deterministic order (to keep override behavior stable).
- Ensure source-id stability where needed by your workflow:
  - PAK ids depend on PAK registration order.
  - Loose cooked ids depend on loose cooked registration order.
- Surface the registered source list in editor diagnostics (debug name, id,
  path).

### 7) Descriptor and resource I/O expectations

- Descriptors are readable as independent streams (one cooked descriptor per
  file).
- Resource tables are readable as independent streams (`*.table`).
- Resource data is readable as independent streams (`*.data`).
- Offsets encoded in descriptors must be meaningful relative to the `AnyReader`
  streams the runtime opens.

### 8) Content Browser integration

- Provide hierarchical browsing of cooked assets based on virtual paths.
- For loose cooked, browse using virtual-path mappings in `container.index.bin`
  (not filesystem scans).
- For `.pak`, browse using embedded browse/trace indexes (container-relative
  paths; no physical paths).

### 9) Override/overlap policy and collision diagnostics

- Implement deterministic last-mounted-wins resolution across sources.
- Detect and report collisions:
  - same virtual path mapping to different `AssetKey` across sources
  - same `AssetKey` present in multiple sources (identical or different bytes)
- Support a strictness policy:
  - editor mode: allow overrides but warn
  - CI/shipping mode: treat selected collision cases as errors

### 10) Dependency closure per container (authoring-time)

- During cook, compute and enforce dependency closure:
  - assets in one cooked root only reference dependencies also cooked into that
    same root
- Provide diagnostics when dependency graphs cross source boundaries.

### 11) Error handling and developer-facing diagnostics

- Present mount-time errors with:
  - cooked root path
  - index schema/version
  - which validation rule failed
- For asset load misses during PIE:
  - show missing `AssetKey` (and, when available, the originating virtual path)
  - list searched sources in the order tried
- Provide a “validate cooked root” action usable without launching PIE.

### 12) Minimal PIE flow enabled by these capabilities

- Persist scene references as virtual paths.
- Ensure referenced assets are cooked into a loose cooked root.
- Resolve `VirtualPath -> AssetKey` using the cooked index.
- Start PIE with deterministic source registration.
- Request runtime loads by `AssetKey` and let the runtime resolve bytes via the
  mounted content sources.

---

## Editor checklist (PIE with loose cooked)

- Mounts: virtual roots are registered; physical roots are canonicalized.
- Virtual paths: normalization and case policy are enforced everywhere.
- Cook outputs: `container.index.bin`, `assets/**`, `resources/*.table` +
  `resources/*.data` are present and complete.
- Index correctness: required sections present; header flags match; no
  duplicates.
- Path hygiene: recorded paths are canonical (container-relative, `/`
  separators, no dot segments).
- Integrity: recorded sizes match; every member other than a resource data file
  has a digest; full validation compares it with the file bytes and verifies
  each data-file resource against its table row.
- Container closure: cooked container includes full dependency closure.
- Registration order: sources register deterministically.
- Resolution: virtual-path lookup uses cooked index; collisions are diagnosed
  deterministically.
- Diagnostics: mount failures and asset misses report searched sources (and
  virtual path when available).

## Complete integrity inventory

The native loose index owns the complete output inventory. Published descriptors
and resource table/data files have canonical relative paths and exact sizes.
Every member except a resource data file has a SHA-256 digest. A resource data
file (`buffers.data`, `textures.data`, `scripts.data`, `physics.data`) records
an all-zero digest: its resources are appended by many producers, so no one
holds a whole-file digest, and each resource is instead verified against the
SHA-256 in its table row. Inventory membership is complete. The index's own
digest lives in the selecting publication record; `.generation.lock`,
`container.writer.lock` and the `.leases` folder are the explicit non-content
exceptions. A file an earlier commit listed is retired: the index lists it
with the commit that retired it, and it may be gone. A writer's output that no
commit lists (a content-named file, an atomic publication's sibling) and a
data file's bytes past its committed length are uncommitted
(`kUncommitted`, `kUncommittedTail`): `ValidateContent`, which a mount runs,
accepts them, and `CheckContent` reports them.

LooseCookedWriter finalization owns inventory production after all writes and
resource tables finish. `Finish` never hashes a file: tables and descriptors are
registered with the digest their producer computed as it wrote them
(`RegisterExternalFile(kind, relpath, digest)`), and data files with
`RegisterExternalDataFile(kind, relpath)`, which records only the measured size.
Native validation checks bytes, membership and bounds. Neither timestamps nor
zero digests on non-data files establish integrity.

Full verification (`CheckContent`/`ValidateContent` with
`IntegrityCheck::kFull`, `CookedContent::VerifyStorage(kFull)` and verified
loose mounts) checks each resource of a data file against the SHA-256 of its
table row. Its failure reasons are `kMalformedTable`, `kResourceOutOfBounds` and
`kResourceDigestMismatch`, the latter two naming the row number.

`LooseCookedIndex::CheckContent` has two overloads: a cooked root path (the real
file system) and an `lc::RootFiles`, a listing plus open interface
(`lc::ListFolder` lists a real folder; links and junctions are `kLink`).
`CookedContent::VerifyStorage` (Cooker) runs the same check over the cooker
storage port, so both report the same failures: unindexed files (`kUnexpected`),
links, non-regular entries, missing files, size mismatches, resources outside
their data file, and, with `kFull`, every digest. `VerifyStorage` throws
`data::CookedFormatError` with a defect mapped from the first issue:

| Issue                      | Defect                             |
| -------------------------- | ---------------------------------- |
| `kMissing`                 | `kMissingPart`                     |
| `kUnexpected`              | `kInconsistent`                    |
| `kSizeMismatch`            | `kSizeMismatch`                    |
| digest mismatches          | `kDigestMismatch`                  |
| links, non-regular entries | `kInvalidPath`                     |
| `kResourceOutOfBounds`     | `kOutOfBounds`                     |
| `kMalformedTable`          | `FileIntegrityIssue::table_defect` |

Resource table rows, in a pak and in a loose root alike, follow one rule
(`data::RequireResourceRow`, `Data/ResourceDescriptor.h`): reserved bytes are
zero, a texture row records the compression its format implies, a row of a
payload has its SHA-256, and an empty row (the absent sentinel) has no digest.

The index reader refuses two content paths that differ only in case
(`kDuplicate`), since a case-insensitive file system holds them in one file; the
loose cooked writer refuses them with `LooseCookedIdentityCollision`. When the
writer publishes a new index it deletes every file the previous index listed
that the new index does not list, of any kind (descriptors whose path changed
included); a path the new index lists stays.
`LooseCookedWriteResult::removed_files` lists them in path order.

Concurrent imports into one root retire participation before awaiting the shared
index publication. Successful completion guarantees that publication finished;
all participants receive its source identity. A participant's import diagnostics
remain local, while publication failure or an aborted cohort reaches every
waiter. The registry owns this barrier, so batch dependency scheduling uses job
outcomes without a separate root-publication tracker.

Inspector exports the native inventory. Protected readers may reuse verification
only within the same valid read lease. Managed publication/provenance references
that inventory instead of duplicating descriptor/file proofs or rehashing merely
to translate proof models. Keep consumed-input and reuse fingerprints distinct.

The current loose index format
([PakFormatVersions.inc](../../Data/PakFormatVersions.inc)) requires recooking
earlier content. Qualification covers tampered/truncated data, absent/extra
files, changed index, stale leases and measured removal of redundant file reads.
This does not remove incremental staging copies; that is a different
writer-granularity problem.

Runtime admission checks the current index format, canonical paths, complete
membership, exact sizes and bounds. Full SHA-256 verification (files and data-file resources) is mandatory for
cook/publication validation and explicit integrity checks; normal mounts retain
opt-in `verify_content_hashes`. Metadata-only admission does not detect
same-size payload corruption. This avoids scanning every resource blob before
normal loading. UE5.7's `IPlatformFilePak.cpp` likewise separates
`ShouldCheckPak` whole-container checks from requested-block signature
verification; Oxygen does not add block signatures in this slice.

A verified result belongs to the exact index digest and protected file opening.
The editor retains its write-denying member handles and rechecks membership;
`.generation.lock` alone is cooperative generation-lifetime protection, not
proof against member edits. Unprotected readers cannot cache verification by
path, file timestamp or generation name. Input fingerprints remain separate.

A cooked root holds one mount, the one its asset identities name
(`/Content/Scenes/A/B.oscene` lives at `Scenes/A/B.oscene` under a root of the
`Content` mount). A tool keeps sibling containers in roots it owns:
RenderScene cooks the examples project's scenes into its own `.cooked/Showcase`,
and a record-only model import retains generations in the caller's
`<generations_root>/<source_identity>/<generation>`; the model import records
stay in their models' mounts, at `<model identity>.import.json`. No container
includes another container's files or ignores arbitrary nested indexes.
