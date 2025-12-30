# Loose cooked content (filesystem-backed cooked source)

This document specifies how Oxygen Content loads **cooked Oxygen runtime formats** from a **directory of loose cooked files** (“loose cooked”), in addition to `.pak` containers.

The goal is a single content model with two container forms:

- **Loose cooked** for fast editor iteration (Play-in-Editor).
- **PAK** for shipping and reproducible builds.

Related:

- Conceptual architecture and async pipeline: `truly-async-asset-loader.md` and `overview.md`
- Dependency tracking and cache semantics: `deps_and_cache.md`

---

## Scope and constraints

- No runtime import of source formats (FBX/GLTF/PNG/etc.). The editor must cook to runtime formats first.
- Scenes are not cooked in this workflow; scenes are edited live and reference assets by **virtual path**.
- Runtime loads assets by `data::AssetKey` and resources by `ResourceKey`.
- Editor responsibilities bridge **virtual paths** to runtime identifiers.

---

## Goals

- Provide a filesystem-backed cooked source that behaves like a container from the loader’s point of view.
- Keep loader decode logic container-agnostic (loaders do not care whether bytes come from PAK or a directory).
- Preserve identity and caching semantics:
  - `data::AssetKey` is the canonical asset identifier.
  - `ResourceKey` uniquely identifies a resource by `(source_id, resource_type, resource_index)`.
- Enable deterministic editor workflows: stable identity, stable resolution, actionable diagnostics.

## Non-goals

- Runtime import of source formats.
- Hot reload (see the dedicated hot reload design doc).
- GPU uploads/residency/budgets.

---

## Key terms

- **Virtual path**: editor-facing identity persisted by scenes, e.g. `/Content/Materials/DarkWood.mat`.
- **Cooked root**: a directory containing cooked artifacts (descriptors, resource tables/data, and an index).
- **Content source**: runtime-facing provider of cooked bytes registered with `AssetLoader` (PAK or loose cooked).
- **Descriptor**: per-asset cooked bytes consumed via `LoaderContext::desc_reader`.
- **Resource tables/data**: cooked resource lookup tables and bulk payload streams consumed via `LoaderContext`.

---

## Core invariants

1. **Intra-source references only**

   Cooked assets in a source may reference only assets/resources in the *same* source.

   This is a runtime correctness requirement: the loader does not support cross-source dependency edges.

2. **Source id segregation is explicit**

   `ResourceKey` encodes a 16-bit **source id**. Source ids are assigned by the runtime as follows:

   - PAK sources use dense ids starting at `0` in PAK registration order.
   - Loose cooked sources use ids starting at `0x8000` in loose-cooked registration order.
   - `0xFFFF` is reserved for synthetic/buffer-backed sources.

   These ranges are part of the contract and are centralized in `Oxygen/Content/Constants.h`.

3. **Async loader contract applies equally to loose cooked**

   The loader is async-first:

   - Worker threads may perform blocking I/O and CPU decode.
   - Only the owning thread may mutate loader state (mounts, cache publish, dependency graph mutation, callbacks).
   - Loader decode must not call back into `AssetLoader` (dependencies are recorded via a collector).

---

## Runtime architecture

### Content sources

`AssetLoader` mounts cooked content sources and resolves assets by `data::AssetKey`.

- `.pak` containers are adapted via an internal PAK-backed content source.
- Loose cooked roots are adapted via an internal directory-backed content source.

Both provide:

- Asset discovery by `data::AssetKey`.
- Readers for descriptors (`desc_reader`).
- Readers/tables for resource access (`data_readers`, resource tables).

### Mounting and resolution order

- Runtime mounting API:
  - `AssetLoader::AddPakFile(path)`
  - `AssetLoader::AddLooseCookedRoot(path)`
- Mount operations are owning-thread only.
- Asset resolution is **first-match-wins** in source registration order.

This ordering is editor policy: if multiple mounted sources overlap (engine content, project content, overrides), the editor must register them deterministically.

### Virtual paths are resolved above the loader

Runtime Content APIs are keyed by `AssetKey`/`ResourceKey` and do not interpret virtual paths.

For editor/tooling workflows, `VirtualPathResolver` exists as a helper:

- It maps canonical virtual paths to `data::AssetKey` using mounted cooked indexes (and PAK browse indexes when available).
- It uses **first-match-wins** in registration order.
- It logs a `WARNING` when the same virtual path exists in multiple mounts but maps to different `AssetKey`s.

---

## Loose cooked on-disk layout

Loose cooked content replicates the conceptual structure of a PAK, split across files.

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

Offsets stored inside cooked descriptors are interpreted relative to the `AnyReader` stream opened by the mounted source for the relevant `*.data` file.

### `container.index.bin` (required)

The cooked index is required for loose cooked roots. It provides:

- `AssetKey -> descriptor relative path`.
- Optional virtual path mapping (`VirtualPath <-> AssetKey`) when enabled by index header flags.
- Resource file metadata (names, sizes, optional SHA-256 digests).

Mount-time expectations:

- The index must be present, parseable, and versioned.
- Index header flags must be self-consistent.
- Section layout must be valid (no overlaps, no out-of-range spans).
- Referenced descriptor and resource files must exist.
- If a non-zero SHA-256 digest is recorded for a referenced file, the runtime validates it on mount.
- All paths recorded in the index must be canonical:
  - container-relative, `/` separators
  - no `..`, no `\\`, no `//`
  - virtual paths start with `/` and contain no dot segments
- Duplicate records are rejected (e.g. duplicate `AssetKey`, duplicate virtual-path strings).

---

## Override/overlap policy and collision diagnostics

Overlaps are expected (engine content + project content + user overrides), but must remain deterministic.

Rules:

- `AssetLoader` resolves `AssetKey` using first-match-wins in source registration order.
- `VirtualPathResolver` resolves `VirtualPath -> AssetKey` using first-match-wins in mount registration order and logs collisions when the same virtual path maps to different `AssetKey`s.

Strictness policy is editor-owned:

- Editor/PIE: allow overrides but warn deterministically.
- CI/shipping validation: treat selected collision cases as errors.

---

## Editor requirements (Play-in-Editor with loose cooked)

This section merges the editor capabilities required to use loose cooked roots as a runtime content source.

### 1) Mount points and namespace management

- Maintain a mount registry mapping `virtual_root -> physical_root` for authoring.
- Maintain a runtime mount list of cooked sources to register at PIE startup:
  - loose cooked roots (directories)
  - `.pak` containers when desired
- Normalize physical roots to avoid duplicates (absolute/canonical/normalized separators).
- Support multiple sources under one virtual root (engine + project + overrides).

### 2) Canonical virtual path rules

- Normalize and validate virtual paths according to the single source of truth:
  - [virtual-paths.md](../../../../../Oxygen.Assets/docs/virtual-paths.md)
- Establish and enforce a case policy (recommended: case-sensitive for identity; handle platform case quirks in UI).
- Use one canonicalization implementation consistently for browser, scenes, cook inputs, and lookup.

### 3) Cook pipeline must produce a valid loose cooked root

- Provide a Cook action that writes a deterministic cooked root containing:
  - `container.index.bin`
  - cooked descriptors under `assets/**`
  - cooked resource tables/data under `resources/*.table` and `resources/*.data`
- Ensure directory layout and naming are stable and mirror the virtual-path hierarchy for descriptors.
- Support incremental cook (regenerate only impacted outputs).
- Support atomic cook updates (stage then swap; avoid partial roots).
- Ensure internal container consistency:
  - descriptors reference valid rows in the corresponding resource tables
  - resource data offsets are valid for the referenced `*.data` files

### 4) Deterministic identity assignment and lookup

- Assign `AssetKey` deterministically from virtual identity (per the project’s canonical hashing scheme).
- Persist or regenerate mappings via `container.index.bin` so they are authoritative and reproducible.
- Provide lookup in both directions:
  - `VirtualPath -> AssetKey`
  - `AssetKey -> VirtualPath`

### 5) Build and validate `container.index.bin`

- Write a versioned index with required mappings and metadata:
  - `AssetKey -> descriptor relative path`
  - virtual-path mapping as enabled by header flags
  - resource file names and metadata
  - per-file size metadata and optional SHA-256 digests
- Enforce index correctness at cook time (fail early):
  - header flags consistent with included sections
  - no overlapping/invalid section ranges
  - no duplicate records
  - reject unknown/unsupported file-kind values
  - referenced paths are container-relative and canonical
- Provide an editor-facing validation command that:
  - verifies referenced files exist
  - verifies recorded sizes
  - verifies hashes when present
  - reports actionable diagnostics

### 6) Runtime provisioning for PIE

- Register cooked sources with the runtime asset loader at PIE startup.
- Register sources in a deterministic order (to keep override behavior stable).
- Ensure source-id stability where needed by your workflow:
  - PAK ids depend on PAK registration order.
  - Loose cooked ids depend on loose cooked registration order.
- Surface the registered source list in editor diagnostics (debug name, id, path).

### 7) Descriptor and resource I/O expectations

- Descriptors are readable as independent streams (one cooked descriptor per file).
- Resource tables are readable as independent streams (`*.table`).
- Resource data is readable as independent streams (`*.data`).
- Offsets encoded in descriptors must be meaningful relative to the `AnyReader` streams the runtime opens.

### 8) Content Browser integration

- Provide hierarchical browsing of cooked assets based on virtual paths.
- For loose cooked, browse using virtual-path mappings in `container.index.bin` (not filesystem scans).
- For `.pak`, browse using embedded browse/trace indexes (container-relative paths; no physical paths).

### 9) Override/overlap policy and collision diagnostics

- Implement deterministic first-match-wins resolution across sources.
- Detect and report collisions:
  - same virtual path mapping to different `AssetKey` across sources
  - same `AssetKey` present in multiple sources (identical or different bytes)
- Support a strictness policy:
  - editor mode: allow overrides but warn
  - CI/shipping mode: treat selected collision cases as errors

### 10) Dependency closure per container (authoring-time)

- During cook, compute and enforce dependency closure:
  - assets in one cooked root only reference dependencies also cooked into that same root
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
- Request runtime loads by `AssetKey` and let the runtime resolve bytes via the mounted content sources.

---

## Editor checklist (PIE with loose cooked)

- Mounts: virtual roots are registered; physical roots are canonicalized.
- Virtual paths: normalization and case policy are enforced everywhere.
- Cook outputs: `container.index.bin`, `assets/**`, `resources/*.table` + `resources/*.data` are present and complete.
- Index correctness: required sections present; header flags match; no duplicates.
- Path hygiene: recorded paths are canonical (container-relative, `/` separators, no dot segments).
- Integrity: recorded sizes match; hashes validate when digests are provided.
- Container closure: cooked container includes full dependency closure.
- Registration order: sources register deterministically.
- Resolution: virtual-path lookup uses cooked index; collisions are diagnosed deterministically.
- Diagnostics: mount failures and asset misses report searched sources (and virtual path when available).
