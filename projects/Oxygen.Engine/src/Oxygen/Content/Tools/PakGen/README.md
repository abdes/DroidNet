# PakGen – Oxygen Engine Content Pack Generator

> Deterministic, test‑driven pack (PAK) builder for Oxygen Engine content specs.

PakGen converts a high‑level content specification (YAML / JSON) into a binary
`.pak` file plus an optional machine‑readable manifest. It replaces an earlier
monolithic script with a layered, modular architecture focused on **determinism**,
**extensibility**, and **strong regression testing**.

---

## Key Features

- Layered pipeline: spec loading → normalization → validation → planning → packing → writing → (optional) manifest.
- Deterministic builds (`--deterministic`): identical bytes & hashes for identical inputs.
- Dry‑run planning (`plan` command) to inspect layout without writing a file.
- Optional manifest emission with file size, region summaries, counts & hashes.
- Deep binary diffing of two pak files (`diff` command) with structured JSON output.
- Structured reporters (plain, rich TTY UI, JSONL events, silent) + adjustable verbosity.
- Strong invariants & regression protection (layout snapshots, CRC, SHA256, size checks).
- Extensible internal model (dataclasses) prepared for future resource/asset types.

---

## Installation / Development

PakGen is a Python 3.11+ package (PEP 621). From the `PakGen` directory:

```pwsh
# Editable install with all optional extras (colorized output, YAML, dev tooling)
pip install -e .[dev,color,yaml]

# Run tests
pytest -q
```

(Within the larger Oxygen Engine repo you may already have a virtual env configured – reuse it.)

---

## CLI Overview

PakGen exposes a single executable `pakgen` with subcommands:

| Subcommand | Purpose |
|-----------|---------|
| `build`   | Build a pak from a spec file. |
| `plan`    | Compute & print layout (no write). |
| `diff`    | Deep diff two existing pak files. |
| `validate`| (Reserved) Validate a spec only. |
| `inspect` | (Reserved) Inspect a pak file. |

Global flags:

- `-v/--verbose` (repeatable) – increase verbosity (affects reporters & logging).
- `-r/--reporter {plain,rich,json,silent}` – choose output backend.

### Build

```pwsh
pakgen build content.yaml output.pak --deterministic --emit-manifest output.manifest.json
```

Key flags:

- `--emit-manifest <path>`: write manifest JSON (opt‑in).
- `--deterministic`: enforce deterministic ordering / layout (recommended for CI).
- `--simulate-material-delay <seconds>`: test helper (introduces artificial per‑material delay).

### Plan (Dry Run)

```pwsh
# Human summary (plain reporter)
pakgen plan content.yaml --deterministic

# JSON plan (machine readable)
pakgen plan content.yaml --deterministic --json > plan.json
```

Outputs a `PakPlan` summary: file size, region offsets/sizes, table counts, padding.

### Diff

```pwsh
pakgen diff old.pak new.pak -r json > diff.json
```

Exit code is non‑zero if differences are detected. Structured JSON includes
section summaries & per‑field differences enabling CI regression gates.

### Reporter Backends

- `plain` (default): concise line‑oriented status.
- `rich`: styled, progress UI (TTY only; falls back to plain if non‑TTY).
- `json`: JSON Lines event stream suitable for ingestion.
- `silent`: suppress non‑essential output (errors still surface via exit codes).

Use `-v`, `-vv`, etc. to increase detail (implementation gating within reporters).

---

## Deterministic Builds

Determinism mode removes nondeterministic influences (ordering, transient
timestamps, random defaults) ensuring repeatable bytes and stable hash triplets.
This powers cache keys, reproducibility audits, and binary diff minimization.

Hashes exposed (in manifest when enabled):

- `spec_hash`: SHA256 of canonical normalized spec (sorted key JSON).
- `pak_crc32`: CRC32 of file bytes.
- `sha256`: Full file SHA256.

Two `pakgen build --deterministic` invocations on the same inputs must produce
identical `sha256` and `pak_crc32` values; tests enforce this.

---

## Manifest (v1 Minimal)

Opt‑in JSON artifact summarizing the build:

```json
{
  "version": 1,
  "file_size": 123456,
  "deterministic": true,
  "regions": [{"name": "texture_region", "offset": 64, "size": 4096}],
  "counts": {"regions": 4, "tables": 3, "assets_total": 5, "materials": 2, "geometries": 1},
  "pak_crc32": "0x89abcdef",
  "spec_hash": "f3e1...",
  "sha256": "9a7b..."
}
```

Future expansions (not yet implemented): per‑asset entries, statistics, padding
breakdown, per‑resource hashes, warnings.

---

## High‑Level API (Python)

Programmatic use mirrors the CLI:

```python
from pathlib import Path
from pakgen.api import BuildOptions, build_pak, plan_dry_run

# Compute plan only
plan, plan_dict = plan_dry_run("content.yaml", deterministic=True)
print(plan.file_size, plan_dict["regions"])  # machine readable info

# Build
opts = BuildOptions(
    input_spec=Path("content.yaml"),
    output_path=Path("output.pak"),
    manifest_path=Path("output.manifest.json"),
    deterministic=True,
)
result = build_pak(opts)
print("Wrote", result.bytes_written, "bytes")
```

All validation errors raise `ValueError` with codes/messages aggregated.

---

## Spec Format (Conceptual Overview)

A spec groups resources (buffers, textures, audio) and higher‑level assets
(materials, geometries, etc.). Normalization & validation ensure:

- Required fields present & typed.
- GUIDs / keys normalized.
- Cross‑references (e.g., material → texture indices) are in range.
- Alignment & size invariants pre‑validated before packing.

(See `pak_gene_refactor.md` for the deeper model and phase roadmap.)

---

## Testing & Invariants

The test suite covers:

- Pack function size invariants & descriptor layout.
- Planner alignment, padding, region overlap safety.
- Binary diff regression (golden file parity).
- CRC / structural inspection roundtrip.
- Deterministic build hashing.
- Manifest emission (presence / counts / hash population).
- Validation error scenarios & boundary limits.

Run locally:

```pwsh
pytest -q
```

---

## Roadmap & Design

The refactor / evolution plan (phases, invariants, future enhancements) lives in
[`pak_gene_refactor.md`](./pak_gene_refactor.md). Key pending items:

- Extended metrics & statistics enrichment.
- Extensibility registries for new resource types.
- Optional fuzz / property testing harness.

---

## Versioning & Stability

Current version: `0.1.0` (pre‑release, APIs may evolve). Deterministic output
and manifest schema fields may expand but existing keys are treated as stable.

---

## License

BSD 3‑Clause – see [LICENSE](../../../../../../LICENSE).

---

## Contributing

- Keep new functionality phase‑scoped; extend tests first.
- Preserve determinism (add regression tests when altering layout / ordering).
- Prefer pure functions for planning / packing; side effects isolated to writer.
- Augment manifest only with backwards‑compatible additions (new keys). Rename
  or removal requires version bump.

---

## Quick Reference

| Task | Command |
|------|---------|
| Build pak | `pakgen build spec.yaml out.pak` |
| Deterministic build + manifest | `pakgen build spec.yaml out.pak --deterministic --emit-manifest out.manifest.json` |
| Plan only (human) | `pakgen plan spec.yaml` |
| Plan only (JSON) | `pakgen plan spec.yaml --json` |
| Diff | `pakgen diff old.pak new.pak` |
| Tests | `pytest -q` |

---

Feedback & improvements welcome. See issues on the main repository.
