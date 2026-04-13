# PAK Format Evolution Ideas

**Date:** 2026-02-28
**Status:** Design / Backlog Ideas

None of these is implemented yet!

## 1. Preload Bundles

Add per-asset preload bundle metadata:

- bundle_id (Startup, Near, OnDemand, custom ids)
- ordered resource refs per bundle
- hard fail if bundle references missing resources

## 2. Dependency Graph Metadata

Persist explicit dependency edges:

- hard_deps (must load first)
- soft_deps (optional, fallback allowed)
- preload_deps (needed before first use)
- cycle detection at cook time

## 3. Chunk/Install Groups

Add install/streaming chunk ids:

- asset-level chunk_id
- region block-level chunk manifest (size/hash list)
- support staged installs and DLC-style packaging

## 4. Virtualized Bulk Payload Indirection (optional but powerful)

Keep regions, but add indirection option:

- descriptor points to payload_ref instead of only raw file offset
- payload_ref can resolve to local region block or shared external blob
- enables dedup/shared cache/remote fetch without changing asset descriptors

## 5. Unified Async I/O Contract

Define one loading contract across all regions:

- aligned block read units
- request priority classes
- cancellation and timeout semantics
- deterministic completion/error codes

## 6. Patch-Friendly Content Addressing

Make patching block-based and deterministic:

- stable block hashes for payload chunks
- per-asset block map table
- changed-asset patch generation by block diff, not full-file replacement
