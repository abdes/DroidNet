# Oxygen.Cooker.PakTool

`Oxygen.Cooker.PakTool` builds Oxygen `.pak` archives from cooked content and
emits the sidecar artifacts required for patching, publishing, and CI
automation.

It is the native Oxygen packaging CLI. It wraps the shared pak-domain pipeline
in `src/Oxygen/Cooker/Pak` and does not implement a second planner or writer.

## Key Capabilities

- Full pak builds from loose-cooked roots and/or existing pak sources
- Patch pak builds against one or more published base catalogs
- Published pak catalog sidecars for later patch runs
- Published patch manifests for compatibility enforcement
- Safe staged publication of pak/catalog/manifest outputs
- Optional structured build report emission
- Staged sealing of loose-cooked external script assets into embedded-source pak
  inputs

## Build

`PakTool` is part of the normal Oxygen.Engine build.

Example:

```powershell
cmake --build out/build-vs --target Oxygen.Cooker.PakTool --config Debug -- /m:6
```

The executable is produced under `out/build-vs/bin/<Config>`.

## Usage

Run help for the full CLI:

```powershell
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe --help
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe patch --help
```

Commands:

- `build`
- `patch`

## Core Options

Common options:

| Option                      | Meaning                                    |
| --------------------------- | ------------------------------------------ |
| `--loose-source <dir>`      | Repeatable loose-cooked source root        |
| `--pak-source <path>`       | Repeatable existing pak source             |
| `--dependency-pak <path>`   | Repeatable dependency pak, lowest first    |
| `--out <pak-path>`          | Final pak output path                      |
| `--catalog-out <path>`      | Final pak catalog sidecar path             |
| `--content-version <u16>`   | Pak content version                        |
| `--source-key <uuidv7>`     | Canonical lowercase UUIDv7 source identity |
| `--diagnostics-file <path>` | Optional structured build report           |
| `--non-deterministic`       | Disable deterministic build behavior       |
| `--embed-browse-index`      | Request browse index embedding             |
| `--no-crc32`                | Disable pak CRC output                     |
| `--fail-on-warnings`        | Escalate warnings to build failure         |
| `--mount <Name>=<dir>`      | Repeatable source mount for script sealing |
| `--mounts <file>`           | Mounts file applied above `.mounts.json`   |
| `--root <dir>`              | Project root whose `.mounts.json` applies  |
| `--quiet`                   | Suppress non-error console output          |
| `--no-color`                | Disable ANSI color in CLI output           |

`build`:

- `--manifest-out <path>` optional

`patch`:

- `--base-pak <path>` repeatable, required
- `--manifest-out <path>` required

## Lower Layers

A pak can rely on paks mounted beneath it in two ways. Both are recorded in the
pak's catalog and manifest by identity (source key, content version, catalog
digest), and the runtime refuses to mount a pak whose requirements do not hold.

| Contract   | Option             | Commands       | Runtime requirement                     |
| ---------- | ------------------ | -------------- | --------------------------------------- |
| Dependency | `--dependency-pak` | build, patch   | Mounted below, in order; others between |
| Patch base | `--base-pak`       | patch (needed) | The contiguous layers right below it    |

- A **dependency** provides assets the pak references without containing them,
  the way game content references the engine content pak (`engine.pak`). The
  planner resolves asset references into dependencies, checks each referenced
  asset's type, and copies nothing.
- A **patch base** is the exact stack a patch's deltas apply to: the patch
  classifies every asset as created, replaced, deleted or unchanged against its
  bases, and its deletions hide base assets. A full build has no bases.

```powershell
Oxygen.Cooker.PakTool.exe build --loose-source .cooked/Content `
  --dependency-pak share/oxygen/engine.pak --out .build/pak/examples.pak `
  --catalog-out .build/pak/examples.catalog.json --content-version 1 `
  --source-key 018f8f8f-1234-7abc-8def-0123456789ab
```

A dependency matches only the exact catalog the pak was built against, so a pak
is rebuilt when its dependency's content changes.

## Published Artifacts

`PakTool` publishes:

- `.pak`
- pak catalog sidecar JSON
- patch/full manifest JSON when requested
- structured build report JSON when `--diagnostics-file` is requested

The catalog sidecar is the authoritative patch input artifact. The build report
is informative only.

## Script Sealing

Loose-cooked script assets may still reference external source files during
authoring workflows. `PakTool` seals those inputs before invoking the pak
builder:

- already-embedded script source/bytecode is preserved as-is
- external-source script assets are rewritten into staged embedded-source pak
  inputs
- no compilation occurs during pak build
- no bytecode is invented if it was not already cooked
- the source cooked root is never mutated

An external script stores its source as a mount path,
`/<Mount>/<path in mount>`. Bind each mount the scripts name with
`--mount <Name>=<dir>`, the `.mounts.json` hierarchy of `--root <dir>` or a
`--mounts <file>`, resolved as ImportTool resolves them; without any of these
there are no source mounts. Each name has one folder, with no search.

| Code                                                 | Meaning                                  |
| ---------------------------------------------------- | ---------------------------------------- |
| `paktool.prepare.source_mount_invalid`               | A `--mount` that is not `<Name>=<dir>`   |
| `paktool.prepare.source_mounts_invalid`              | The mount table has errors, each listed  |
| `paktool.script_seal.external_source_not_mount_path` | The stored path is not a mount path      |
| `paktool.script_seal.external_source_mount_unbound`  | No option or mounts file binds its mount |
| `paktool.script_seal.external_source_unresolvable`   | The file is not in its mount's folder    |

## Patch Precedence

Mounted pak resolution is stack-based:

- the last mounted source wins for subsequent loads
- tombstones in higher-priority patch layers still mask lower layers

This applies both when all sources are mounted before load and when a patch is
mounted after the base content has already been used. Asset-loader cache
identity for explicit-source loads must therefore remain mount-instance based,
not `SourceKey` based.

## Examples

RenderScene packages the cooked examples with its `Tools/pak`; see
[Cook, package and play the examples](../../../../../Examples/RenderScene/README.md#cook-package-and-play-the-examples).

Full build from a loose-cooked root:

```powershell
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build `
  --loose-source Examples/RenderScene/.cooked/Content `
  --out Examples/RenderScene/.build/pak/examples.pak `
  --catalog-out Examples/RenderScene/.build/pak/examples.catalog.json `
  --manifest-out Examples/RenderScene/.build/pak/examples.manifest.json `
  --content-version 1 `
  --source-key 018f8f8f-1234-7abc-8def-0123456789ab
```

Patch build against a published base catalog:

```powershell
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe patch `
  --loose-source Examples/RenderScene/.cooked/Content `
  --base-pak Examples/RenderScene/.build/pak/examples.pak `
  --out Examples/RenderScene/.build/pak/examples-patch-1.pak `
  --catalog-out Examples/RenderScene/.build/pak/examples-patch-1.catalog.json `
  --manifest-out Examples/RenderScene/.build/pak/examples-patch-1.manifest.json `
  --content-version 1 `
  --source-key 018f8f8f-1234-7abc-8def-0123456789ab
```

Build with an emitted diagnostics report:

```powershell
out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build `
  --loose-source Examples/RenderScene/.cooked/Content `
  --out Examples/RenderScene/.build/pak/examples.pak `
  --catalog-out Examples/RenderScene/.build/pak/examples.catalog.json `
  --diagnostics-file Examples/RenderScene/.build/pak/examples.report.json `
  --content-version 1 `
  --source-key 018f8f8f-1234-7abc-8def-0123456789ab
```

## Related Schemas

- pak catalog schema:
  `src/Oxygen/Cooker/Pak/Schemas/oxygen.pak-catalog.schema.json`
- build report schema:
  `src/Oxygen/Cooker/Tools/PakTool/Schemas/oxygen.pak-build-report.schema.json`

## Versions

`versions` prints the version of every cooked format and asset the tool reads
and writes, from `PakFormatVersions.inc`; `--json` prints them as one object.

## Exit Codes

| Code | Meaning                                          |
| ---- | ------------------------------------------------ |
| `0`  | Build completed with no errors                   |
| `1`  | CLI usage or argument error                      |
| `2`  | External input or filesystem preparation failure |
| `3`  | Build completed but emitted error diagnostics    |
| `4`  | Unhandled runtime failure or publish failure     |

## Validation Notes

The shipped validation set covers:

- native pak-domain tests
- PakTool CLI/request/report/publication/process-contract tests
- `PakDump` inspection of emitted full and patch paks
- failure-path publication semantics
- repo-scale patch classification
- live runtime patch mounting after base content has already been loaded
