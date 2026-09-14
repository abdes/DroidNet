# ED-M07B browser workload

Recorded 2026-09-14 using the packaged Release WorldEditor UI tests.

## Fixture and measurement

The production pipeline imports one glTF source and cooks a project with 998
scalar material descriptors and one authored scene: 1,000 authored inputs.
The scene has 98 geometry nodes, hierarchy, one perspective camera and one sun.
It uses every exposed built-in shape, imported meshes, and shared and distinct
materials. Native observations verify 27,588 triangles across the geometry nodes.

A live 1920×1080 editor viewport uses conventional shadows, a 1/60-second fixed
simulation step, manual EV 9.7 and ACES fitted tone mapping. Graphics validation
and ImGui are disabled. Camera framing uses the editor viewport default.
Windows scaling is 175%.

Cold loading measures layout activation through the first rendered asset rows.
Search feedback measures a search-text change through the next composition
frame, with 10 warm-up queries and 100 measured queries per layout. Each query
checks its resulting asset count. All authored inputs are current and native
mounting is acknowledged before measurement.

| Layout | Cold catalog/UI | Search feedback p95 |
| --- | ---: | ---: |
| List | 1,975.35 ms | 18.42 ms |
| Tiles | 1,733.12 ms | 19.09 ms |
| Browser budget | 5,000 ms | 250 ms |

Both tests pass. The provider contains 1,027 representations; the test counts
1,000 authored inputs separately from built-ins and generated outputs. Captures
show the filtered rows with Ready status. Scene-opening, command-feedback, GPU
frame-time, lifecycle and requested DPI walkthroughs retain their separate gates.

## Machine and artifacts

- CPU: AMD Ryzen 9 9950X, 16 cores / 32 logical processors.
- RAM: 125.6 GiB reported by Windows.
- Available GPUs: NVIDIA RTX 3080, 10 GiB, driver 610.62; AMD Radeon integrated
  graphics, driver 32.0.21042.62. The runtime uses its default adapter selection.
- OS: Windows 11 Pro 10.0.26200; runtime: .NET 9.0.20.
- Results: `artifacts/TestResults/m07b-rendered-catalog-final.trx`.
- Captures, input-file hashes and complete Content archives are attached to
  each result under `artifacts/TestResults/abdes_GIGA_2026-09-14_20_59_33/In`.
- List archive SHA-256:
  `E4BDDF71B06464D4CB9C4CB1C24763C951F204B838EC50B72C783A670C2FF10C`.
- Tile archive SHA-256:
  `69384483A1FAE7BB30CD3CBB002E3B056C66E22A500B83487F1108B162D9E4A1`.

The workload exposed and now exercises the fixes for native admission overflow
(`a6ba9803a`) and flattened descriptor folders (`49c782da8`). Changed test files
have no analyzer or IDE diagnostics after the final Release build.
