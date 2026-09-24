# EX07C — Final validation and closure

**Closed on 2026-09-25.** The omitted caller builds and executions are qualified,
the importer follow-up is reconciled, and the defect discovered during validation
is fixed in `3fb0a8b17`. EX07F remains the final combined acceptance stage.

## Fresh qualification

Only the existing `out/build-ninja` tree was used, with Release and Debug
configurations. No build tree was created. The exposure workload ran in Release
with Tracy off; its implementation deliberately rejects Debug timing runs.

| Owning target                              | Release                              | Debug                                |
| ------------------------------------------ | ------------------------------------ | ------------------------------------ |
| `Oxygen.Scene.EnvironmentComponents.Tests` | Built; **5/5 passed**                | Built; **5/5 passed**                |
| `Oxygen.Vortex.Exposure.Benchmarks`        | Built; **I02 event scenario passed** | Built; Release-only scenario not run |
| `Oxygen.Cooker.AsyncImportCore.Tests`      | **190/190 passed**                   | **190/190 passed**                   |
| `Oxygen.Cooker.AsyncImportTexture.Tests`   | **149/149 passed**                   | **149/149 passed**                   |
| `Oxygen.Cooker.AsyncImportGltf.Tests`      | **32/32 passed**                     | **32/32 passed**                     |
| `Oxygen.Cooker.AsyncImportFbx.Tests`       | **3/3 passed**                       | **3/3 passed**                       |

This is **759 freshly executed cases**, with no failures or skips in the accepted
runs. The caller migration in `ff1746220` is now exercised: RGB atmosphere disk
scale is checked by EnvironmentComponents; the I02 scenario changes and restores
attached directional-light intensity through `EditLight` at event frames 300/360.

The unchanged I02 acceptance path executed all 16 operations, including exposure
mode changes, sharing, view removal/recreation, layout changes and delayed GPU
status delivery. It retained 1,200 CPU status observations and 62 GPU checkpoints.
The accepted run used automatic calibration: **6,000 measured frames / 43.989 s**,
with complete, valid GPU timing. CPU/GPU preflight passed before launch.
These are correctness/execution proofs; incidental timings are **not a newly
established performance baseline** or a replacement for D/E results.

The initial 2,400-frame attempt passed the transition checks but failed the
existing 30-second sampling minimum at 17.844 s. That rejected attempt is
preserved. The successful rerun used the benchmark's existing automatic frame
count; no assertion or threshold was weakened.

## Defect found and repaired

`TexturePipelineEdgeTest.MaterialPresetsKeepCompressionAndMipChains` aborted on
its second import. `ImportEventLoop::Stop()` released the constructor-owned ASIO
work guard; `Run()` restarted the context without restoring that guard. The next
run could exit while a ThreadPool result was still pending.

Each Run now owns a fresh guard and releases it before restarting the context.
Stop only operates on thread-safe ASIO state. The new regression executes three
successive worker-backed runs on the same loop; the existing multi-policy texture
checks and all four importer suites also pass in both configurations. The actual
glTF and FBX Sponza imports ran successfully; none were skipped for missing assets.

## Importer and cooked-content reconciliation

The existing policy checks verify material BC7 presets and full mip chains,
explicit/source-format choices, and preservation of HDR radiance. Both model
adapters select the material preset unless an explicit override is supplied.
The D3D12 allocation admission guard still applies the hard driver-budget flag
only when a scoped budget owner exists; unowned material resources retain their
ordinary allocation policy. The previously accepted D/E native scene loads and
shutdown checks remain credited for that path.

The active Sponza content is already corrected:

- **72 textures:** 25 BC7 sRGB and 47 BC7 linear; every texture has **13 mips**.
- Every OTEX sidecar exactly matches its resource-table entry and full mip count.
- Total texture payload is **1,610,901,504 bytes**, rather than the rejected
  reimport's 121 RGBA8 textures / 7,539,563,776 bytes.
- The scene and container hashes still match the visually accepted final E
  captures, before and after the importer tests. No accepted content was replaced.

Consequently the old instruction to recook the uncompressed Sponza generation
no longer describes the active application content. Real-source import tests,
policy regressions and current-content inspection close that follow-up.

## Credited earlier C evidence

The archived results were inspected, not rerun or silently expanded:

- **542 native Debug cases**, with zero recorded failures across the final C
  suites and focused image/failure/ABI records.
- **123 PakGen cases**, with no failures/errors/skips.
- **88 distinct named managed/editor/native-bridge cases** across the three TRX
  reports. Their execution totals overlap; duplicate cases are not counted twice.
- The original C scene-v7, atomic-edit, light-property transport, photometry,
  receiver/contact-shadow and per-view failure work remains credited, together
  with the previously recorded 16-scene / 124-asset / 29-resource package and
  user visual acceptance of the conventional-shadow repair.

[F engine-side acceptance](EX07F-acceptance-report.md) subsequently credits these
results. The interactive editor check remains user-owned. C has no remaining
implementation or validation item.

## Durable evidence

[Qualification summary and artifact checksums](validation/ex07c-20260925/summary.json)
contains all fresh test counts, build/source/binary identity, exposure operations,
content inspection and credited historical counts. Original test/build logs,
JSON/TRX/XML reports, rejected attempts and exposure event records are stored
losslessly as gzip files beside it. The historical report retains its original
withdrawn-completion notice as provenance; this report supersedes that notice.

[Authoritative tracker](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).
