# Asset database and derived data cache (DDC)

This document defines the editor-oriented **Asset Database** and **Derived Data
Cache (DDC)** architecture for Oxygen.

It complements `loose_cooked_content.md` and `scenes_and_levels.md` by providing
stable indexing, incremental rebuild, and fast iteration.

---

## Motivation

Loose cooked content enables loading cooked assets from disk, but it does not
solve:

- Stable ownership of `data::AssetKey` across a project.
- Mapping AssetKey ↔ source path.
- Tracking import/cook settings.
- Incremental rebuild when sources/settings change.

Unity, Unreal, and Godot all solve this with a combination of:

- An asset database/registry (GUID ownership and metadata).
- A derived data cache for cooked artifacts.

Oxygen needs the same separation:

- Content loads cooked artifacts.
- Tooling produces cooked artifacts.

---

## Goals

- Provide a project-wide index that maps:
  - AssetKey → source files
  - AssetKey → cooked artifacts (per platform)
  - asset type, tags, dependencies, import settings
- Provide a deterministic DDC key scheme and a cache store implementation.
- Support editor iteration: incremental cook, validation, and fast startup.

## Non-goals

- Runtime shipping format decisions (PAK packing policy).
- GPU residency or renderer pipeline.

---

## Definitions

- Source asset: authoring data (e.g. `.blend`, `.psd`, `.png`, `.gltf`).
- Imported asset: canonical engine representation (often still editable).
- Cooked artifact: platform-optimized binary consumed by Content loaders.

Oxygen Phase 1.5 focuses on **cooked artifacts**; source importers can be added
incrementally.

---

## Asset database

### Responsibilities

- Stable identity: assign/persist `data::AssetKey`.
- Metadata: name, type, labels/tags.
- Dependency graph at the project level:
  - direct references between assets
  - source dependencies for import/cook
- Build inputs:
  - import settings hash
  - tool version
  - target platform

### Storage

Two-tier storage is recommended:

- Authoritative on disk: a simple database file (SQLite is acceptable) or a
  custom binary index.
- In-memory cache for the editor.

The storage choice should optimize for:

- transactional updates
- cross-process robustness
- fast queries by AssetKey and by path

### Suggested records

- Asset record
  - `AssetKey`
  - `AssetTypeId`
  - canonical source path
  - human name
  - import settings blob/hash
  - direct asset dependencies (AssetKey list)

- Artifact record
  - `AssetKey`
  - platform id
  - artifact kind (descriptor, buffer table, texture data, etc.)
  - DDC key
  - output path (optional)

---

## Derived data cache (DDC)

### Purpose

The DDC stores cooked artifacts indexed by a deterministic key so that:

- repeated cooks are fast
- multiple projects can share cache entries
- editor can restart without re-cooking everything

### Key scheme

A DDC key must include all inputs that affect output bytes:

- source content hash (or content address)
- import settings hash
- cooker version
- engine version
- platform triple

Recommended representation:

- canonical string like:
  - `OxygenDDC:v1:<platform>:<toolVersion>:<assetType>:<inputHash>:<settingsHash>`
- plus a strong hash (SHA-256) of that string for filesystem storage

### Store interface (conceptual)

- `bool TryGet(key, out_bytes)`
- `void Put(key, bytes)`
- optional: streaming writes with atomic commit

### Storage layout

For a filesystem DDC:

- partition by prefix to avoid huge directories

```text
DDC/
  ab/
    abcd... .blob
```

### Atomicity

DDC writes must be atomic:

- write to temp file
- fsync/close
- rename to final path

---

## Relationship to loose cooked content

Loose cooked containers are *outputs* of cooking.

The Asset DB + DDC should be able to materialize a loose cooked container by:

- selecting the current target platform
- resolving each asset’s cooked artifact(s)
- writing/refreshing:
  - asset descriptor files
  - resource tables and data files
  - container manifest

---

## Validation and failure modes

- Stale DDC entry: detected by key mismatch; safe fallback is recook.
- Partial writes: avoided by atomic rename.
- Dependency drift: Asset DB graph must be updated during import/cook.

---

## Testing strategy

- DDC key determinism test: same inputs yield same key.
- Atomic commit test: crash during put does not corrupt existing entries.
- Asset DB round-trip test: insert/update/query by AssetKey.

---

## Open questions

- Do we want a single shared DDC per user machine, or per workspace?
- Should the editor enforce AssetKey uniqueness through `.meta` files (Unity)
  style, or a DB-only approach?
