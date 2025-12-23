# Editor capabilities for rendering with loose cooked assets

This document lists **editor capabilities** required to take advantage of the engine runtime support for **loose cooked (filesystem-backed) cooked Oxygen formats**.

Scope and constraints:

- **No runtime import** of source formats (FBX/GLTF/PNG/etc.) during play-in-editor; the editor must **cook** to runtime formats first.
- **Scenes are not cooked** in this workflow; scenes are edited live in the editor and may reference assets by **virtual path**.
- The runtime loads assets by **`AssetKey`** and resources by **`ResourceKey`**; editor responsibilities bridge **virtual paths** to these runtime identifiers.

---

## 1) Mount points & namespace management

The editor must provide a mount system that defines the virtual namespace and supplies the concrete cooked sources to the runtime.

Required capabilities:

- Maintain a **mount registry** mapping `virtual_root → physical_root` for authoring (e.g. `/Content → <Project>/Content`).
- Maintain a **runtime mount list** of cooked sources to register at play-in-editor startup:
  - loose cooked roots (directories)
  - packaged containers (`.pak`) when desired
- Ensure **path normalization** for physical roots to avoid duplicates (absolute/canonical/normalized separators).
- Support mounting **multiple sources under one virtual root** (engine content + project content + user overrides).

---

## 2) Canonical virtual path rules (identity and normalization)

Because virtual paths are editor-facing identity (what scenes persist), the editor must enforce a deterministic normalization policy.

Required capabilities:

- Normalize and validate virtual paths according to the single source of truth: [virtual-paths.md](../../../../../Oxygen.Assets/docs/virtual-paths.md).
- Establish and enforce an explicit case policy (recommended: treat virtual paths as case-sensitive for identity; handle platform case quirks in UI).
- Provide a single canonicalization implementation used consistently by:
  - content browser
  - scene serialization
  - cook inputs
  - virtual-path-to-AssetKey lookup

---

## 3) Cook pipeline that produces a valid loose cooked root

To render with cooked unpacked assets, the editor must be able to produce the runtime-consumable cooked artifacts on disk.

Required capabilities:

- Provide a **Cook** action that writes a deterministic loose cooked root containing:
  - `container.index.bin`
  - cooked descriptor files under `assets/**`
  - cooked resource tables/data under `resources/*.table` and `resources/*.data`
- Ensure the cooked directory layout and naming are stable and match the virtual-path hierarchy for descriptors.
- Implement **incremental cook** (regenerate only impacted cooked outputs).
- Implement **atomic cook updates** (stage outputs in a temp/staging directory and swap into place; avoid partial roots).
- Ensure the cook step produces **internally consistent containers**:
  - descriptors reference valid rows in the corresponding resource tables
  - resource data offsets are valid for the referenced `*.data` files

---

## 4) Deterministic identity assignment & lookup

The editor must provide deterministic identity mapping from virtual paths to runtime identifiers.

Required capabilities:

- Assign `AssetKey` deterministically from the virtual identity (per the design’s required hashing scheme).
- Persist or regenerate the mapping via `container.index.bin` so that it is authoritative and reproducible.
- Provide lookup in both directions (needed for editor UX and play-in-editor resolution):
  - `VirtualPath → AssetKey`
  - `AssetKey → VirtualPath`

---

## 5) Build and validate `container.index.bin`

The loose cooked root is only usable if the index exists and passes runtime validation.

Required capabilities:

- Write a versioned `container.index.bin` that includes required mappings and metadata:
  - `AssetKey → descriptor relative path`
  - virtual-path mapping (as declared by index header flags)
  - resource table/data filenames and metadata
  - per-file size metadata and optional SHA-256 digests
- Enforce index correctness rules at cook time (so play-in-editor fails early, not at runtime mount time):
  - header flags consistent with included sections
  - no overlapping/invalid section ranges
  - no duplicate records (duplicate `AssetKey`, duplicate virtual-path strings, etc.)
  - reject unknown/unsupported `FileKind` values
  - referenced descriptor/resource paths are container-relative and canonical (no `..`, no `\\`, no `//`)
- Provide an editor-facing **validation command** (quick check) that:
  - verifies all referenced files exist
  - verifies recorded sizes
  - verifies hashes when present
  - reports actionable diagnostics

---

## 6) Runtime provisioning for play-in-editor

When starting play-in-editor, the editor must register the cooked sources the runtime will search.

Required capabilities:

- Register **loose cooked roots** with the runtime asset loader at startup (and `.pak` containers when desired).
- Register sources in a **deterministic order** (because `ResourceKey` encodes a 16-bit container id derived from registration order).
- Ensure the same deterministic ordering policy is applied every run (recommended: sort by virtual root, then explicit priority, then stable path string).
- Surface the registered container list in editor diagnostics (debug name, id, root path).

---

## 7) Descriptor and resource IO expectations (editor-owned constraints)

The runtime loaders assume that descriptor and resource bytes are presented consistently regardless of container type.

Required capabilities:

- Ensure cooked outputs match runtime reader expectations:
  - descriptor files are readable as independent streams (each file corresponds to one cooked descriptor)
  - resource tables are readable as independent streams (`*.table`)
  - resource data is readable as independent streams (`*.data`)
- Ensure any offsets encoded in descriptors are meaningful relative to the corresponding `AnyReader` stream the runtime will open for that source.

---

## 8) Virtual-path browsing & Content Browser integration

To “use real content” in the editor and to browse cooked outputs, the editor needs browse/trace support.

Required capabilities:

- Provide hierarchical browsing of cooked assets based on virtual paths.
- For loose cooked:
  - browse using the virtual-path mappings in `container.index.bin` (not by scanning the file system heuristically).
- For `.pak` (when mounted for editor browsing):
  - rely on the embedded path directory described by the design (container-relative paths, no physical paths).

---

## 9) Override/overlap policy and collision diagnostics

When multiple sources overlap under a mount, the editor must apply deterministic resolution and provide diagnostics.

Required capabilities:

- Implement deterministic **first-match-wins** resolution for virtual paths across sources, using ordered `(priority, registration_order)`.
- Detect and report collisions:
  - same virtual path mapping to different `AssetKey` across sources
  - same `AssetKey` present in multiple sources (with identical or different bytes)
- Support a strictness policy:
  - editor mode: allow overrides but warn deterministically
  - CI/shipping mode: treat selected collision cases as errors

---

## 10) Dependency closure per container (authoring-time enforcement)

Because cooked containers must only reference assets/resources inside the same container, the editor must ensure cooks are container-closed.

Required capabilities:

- During cook, compute and enforce a **dependency closure** so that:
  - assets in one cooked root only reference dependencies that are also cooked into that same root
- Provide diagnostics when an asset’s dependency graph crosses container boundaries.

---

## 11) Error handling and developer-facing diagnostics

Loose cooked is meant for fast iteration, so failures should be easy to understand.

Required capabilities:

- Present mount-time errors in editor UI/logs with:
  - cooked root path
  - index version
  - which validation rule failed
- For asset load misses during play-in-editor:
  - show missing `AssetKey` (and, when available, the original virtual path)
  - list searched containers/sources in the order tried
- Provide a “validate cooked root” action usable without launching play mode.

---

## 12) Minimal play-in-editor flow enabled by these capabilities

With the above capabilities in place, the editor can render an edited scene using cooked unpacked assets by:

- Persisting scene references as **virtual paths**.
- Ensuring referenced assets are **cooked** into a loose cooked root.
- Resolving `VirtualPath → AssetKey` using the cooked index.
- Starting play-in-editor with deterministic container registration.
- Requesting runtime loads by `AssetKey` and letting loaders consume descriptor/resource bytes from the loose cooked source.

---

## Editor checklist (PIE with loose cooked)

Use this as a practical “ready to render” checklist.

- Mounts: virtual roots are registered; physical roots are canonicalized.
- Virtual paths: normalization and case policy are enforced everywhere (browser, scene, cook, lookup).
- Cook outputs: `container.index.bin`, `assets/**`, `resources/*.table` + `resources/*.data` exist and are complete.
- Index correctness: required sections present; header flags match; no duplicate `AssetKey` or virtual-path entries.
- Path hygiene: all recorded paths are container-relative and canonical (`/` separators, no `..`, no `\\`, no `//`).
- Integrity: recorded sizes match; hashes validate when digests are provided.
- Container closure: cooked container includes the full dependency closure; no cross-container references.
- Registration order: cooked sources register deterministically so container ids are stable.
- Resolution: `VirtualPath → AssetKey` uses the cooked index; collisions produce deterministic warnings/errors.
- Diagnostics: mount failures and asset misses report searched sources and (when possible) the originating virtual path.
