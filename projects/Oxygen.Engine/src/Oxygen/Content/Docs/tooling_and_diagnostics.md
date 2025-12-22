# Tooling and diagnostics (Content)

This document specifies tooling and diagnostics capabilities for the Content
subsystem.

The goal is to keep docs and behavior aligned and to make Content measurable and
inspectable during editor development.

---

## Goals

- Provide a dependency analyzer export (JSON).
- Provide repeatable performance benchmarks.
- Provide a small set of developer-facing diagnostic hooks.

## Non-goals

- Full editor UI. This document defines data and CLI-facing outputs.

---

## Dependency analyzer

### Output requirements

- Nodes: assets (AssetKey, type, size estimates, load state)
- Edges:
  - asset→asset
  - asset→resource
- Reference counts:
  - current cache refcount (if available)
  - number of dependents

### JSON schema (sketch)

- `version`
- `containers[]`
  - name
  - id
- `assets[]`
  - key
  - type
  - containerId
  - deps.assets[]
  - deps.resources[]
- `resources[]`
  - key
  - type
  - containerId

### Extraction strategy

- In Phase 1.5, it is acceptable to compute dependents by scanning forward maps.
- In later phases, introduce a maintained reverse map for efficient queries.

---

## Benchmarks

Minimum benchmark scenarios:

- cold start load of a set of assets (empty cache)
- warm cache load (hits)
- parallel burst (async) once Phase 2 exists

Metrics:

- IO time
- decode time
- cache hit ratio
- evictions

Benchmarks must be deterministic and runnable in CI.

---

## Diagnostics hooks

- Scoped timing (RAII) around load phases.
- Structured events:
  - asset requested
  - cache hit/miss
  - decode started/ended
  - eviction
  - hot reload invalidation

The diagnostics API must avoid allocations in hot paths.

---

## Testing strategy

- Snapshot test for analyzer JSON (stable ordering required).
- Benchmark sanity test (non-zero timings, monotonic counters).

---

## Open questions

- Do we expose diagnostics as a pull API, push callbacks, or both?
- Should analyzer export include per-chunk metadata once streaming lands?
