# Streaming and chunks (CPU-side)

This document describes runtime-facing **streaming metadata** and
**chunk/partial decode** support within Content.

This is distinct from `chunking.md`, which describes the on-disk layout and
alignment constraints.

---

## Goals

- Allow assets to expose chunk metadata that enables partial acquisition.
- Support progressive decoding phases (header-first, then heavy payload).
- Provide prefetch/priority hints that influence CPU work scheduling.

## Non-goals

- GPU residency budgets and GPU eviction.
- Renderer upload scheduling.

---

## Concepts

- Chunk: a named slice of an asset that can be acquired independently.
- Partial decode: loaders produce a usable minimal representation before full
  decode completes.

Examples:

- GeometryAsset
  - header chunk (bounds, submesh table)
  - LOD chunks

- TextureResource
  - mip-group chunks

---

## Metadata representation

Content should attach chunk metadata to the decoded CPU object:

- chunk id
- byte ranges (descriptor + data)
- dependency requirements (if any)

This metadata must be:

- deterministic
- stable across cooks for the same asset version

---

## API surface (conceptual)

- `PrefetchAsync(AssetKey key, Priority p)`
- `LoadChunkAsync(AssetKey key, ChunkId chunk, CancellationToken ct)`

These APIs build on the async pipeline (`async_cpu_pipeline.md`).

---

## Loader patterns

To support partial decode, loader functions can be structured in phases:

- parse header
- emit chunk metadata
- decode requested chunk(s)

Loaders must remain deterministic and avoid hidden side effects.

---

## Scheduling

Content should allow a scheduling policy to prioritize:

- editor-visible items first
- camera-proximal items
- requested chunks over background prefetch

The first implementation can be a simple priority queue.

---

## Testing strategy

- Partial decode test: header chunk available before full payload.
- Prefetch test: prefetch does not block main thread.
- Priority test: high priority chunk completes before low priority work.

---

## Open questions

- Do we model chunk metadata inside cooked descriptors, or compute it at runtime?
- How do we represent “chunk dependencies” without reintroducing complex graphs?
