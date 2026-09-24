# EX07B completion audit

Status: **EX07B validated.** The pause before C was the instruction at B closure.
Current progression is recorded in the [tracker](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items):
EX07D is now closed and EX07E is active; its work ledger and resume checkpoint
are in the linked tracker.

This closes the reference/instrument gate only. EX07 as a whole remains
`in_progress`. Production physical repairs and their admission tests belong to C;
qualified performance baselines and optimizations belong to D–F.

| B requirement                  | Implementation and evidence                                                                                                                                                                                                                                                                                                                    |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Independent physical oracle    | `Test/Lighting/Reference`: correlated GGX moments, compensated/coupled BRDF, photometry, finite sphere/disk integration and material evaluation. Analytic, independent-integral and high-precision checks qualify the declared query matrices; production LUT interpolation still needs its own certificate.                                   |
| Known-input native probes      | Native ABI/material/UV/photometry/BRDF probes and explicit physical-admission verifier. The previously measured 23 photometry and 565 BRDF residuals remain C repair inputs, not passing physical renderer results.                                                                                                                            |
| Independent image reference    | Test-only full-list GPU evaluation independent of renderer selection, plus serial accumulation, permutations, missing-light controls and clearing. Closing runs retain 331,776 full-list comparisons and 110,592 permutation comparisons per configuration.                                                                                    |
| Material sampling              | 144 original constant-map cases, 240 UNORM/sRGB/BC7 filtering/wrapping/masking cases and 56 mip-selection/blending cases through real forward/deferred material paths. BC7 three-channel normal maps are checked through actual G-buffer writes.                                                                                               |
| Deterministic workload recipes | `LightingWorkload.{h,cpp}` and versioned `LightingWorkloads.json`. Primary: 1,024 lights, 512 point / 512 spot, 1920x1080, 576 guaranteed visible contributing sources throughout the 240-frame motion cycle. Count/distribution, projection, view, shadow, finite/wide-spot and mutation parameters are frozen.                               |
| Runnable primary fixture       | Native preview publishes all 1,024 lights and renders both families; each lights 2,232 of 2,304 preview pixels. The 64x36 preview preserves the primary aspect/frustum and source population. It is quick fixture correctness evidence; the official 1080p correctness/performance matrix remains C/D work.                                    |
| Bounded instruments            | Native CPU phases/GPU timelines, allocator snapshots, deferred-retirement checks and fixed-storage creation counters. Native and synthetic negative controls qualify capacity/overflow, identity, retention, interval and counter behavior. Shared-stage controls and whole-fixture memory attribution are specified in the workload manifest. |
| Collection overhead            | Both full and focused runs remain recorded with their measured limitations. The user explicitly accepted item 3 as done and directed no further long B benchmark runs. No EX07D budget is changed by that acceptance.                                                                                                                          |

## Closing implementation

The sampler matrix exposed a real material-identity omission: UV transforms and
alpha cutoff were serialized to GPU constants but omitted from the deduplication
signature. `MaterialBinder.cpp` now includes those retained inputs. UV coordinates
use their exact values rather than dimensionless scalar quantization. The same
native cases exercise changing offsets, scales and alpha cutoffs; no compatibility
API, shader workaround or format expansion was introduced.

The BC7 fixture independently writes mode-6 endpoint/selector blocks using the
[Microsoft format definition](https://learn.microsoft.com/windows/win32/direct3d11/bc7-format-mode-reference).
It does not use the cooker encoder to derive its expected texels. CPU predictions
decode sRGB before bilinear/trilinear interpolation. Incorrect decode-after-filter
predictions are rejected by 168 negative-control channel cases. The material
`2% + 2e-5` comparison budget is unchanged. Maximum emission error is 0.004248900
and maximum mip error is 0.001879846 in the Release closing run.

## Final affected checks

All builds use `out/build-ninja`.

| Target                                        |          Debug |        Release |
| --------------------------------------------- | -------------: | -------------: |
| `Oxygen.Vortex.LightingImageReference.Tests`  |  5/5, 18.531 s |   5/5, 9.114 s |
| `Oxygen.Vortex.LightingInstrumentation.Tests` | 17/17, 0.047 s | 17/17, 0.029 s |
| `Oxygen.Vortex.MaterialBinder.Tests`          | 34/34, 0.075 s | 34/34, 0.018 s |

Raw evidence is under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07b`:
`closeout-images-{debug,release}.json`, `closeout-cpu-{debug,release}.json`,
`material-identity-{debug,release}.json` and their build/run logs. New workload
and material-test code is oxytidy-clean. `closeout-tidy-final/` records 70 findings
on unchanged MaterialBinder code; the added identity fields have no findings and
no suppression was added.

Stable implementation checkpoints: `91fe62b0e` (material identity and sampling)
and `2cdc585cb` (workload generator, manifest and native preview).

Earlier reference/probe, RenderDoc, memory, counter and benchmark evidence remains
in [the validation record](EX07B-reference-validation.md). No long benchmark or
slow unchanged mathematical suite was repeated for this closeout. New visual
approval is unnecessary for these controlled native readback comparisons;
production lighting repair will require the C/F numerical and visual gates.

## Remaining ownership

- C: repair the physical residuals, complete retained-property/content migration,
  capacity/admission and lifetime behavior; qualify workload correctness.
- D: record correctness-qualified baselines and freeze numeric performance budgets.
- E/F: implement and qualify scalability/resource optimizations and final delivery.

No B exit item remains open. The pause before C applied at B closure and has
since been superseded by user resumption; follow the current tracker for execution.
