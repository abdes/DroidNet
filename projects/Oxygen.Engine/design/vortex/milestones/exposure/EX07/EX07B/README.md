# EX07B — References and instruments

Status: `validated`

| Field     | Summary                                                                   |
| --------- | ------------------------------------------------------------------------- |
| Outcome   | Independent references, native probes and measurement-tool qualification. |
| Remaining | None in the recorded scope.                                               |
| Evidence  | [Validation record](validation.md)                                        |

[Roadmap](../../../../PLAN.md) · [Design index](../../../../lld/README.md)

Status: **EX07B validated.** The pause before C was the instruction at B closure.
Current progression is recorded in the [tracker](../README.md#stages-and-ownership):
A–F and overall EX07 are closed, including [final editor acceptance](../EX07F/validation.md).

This audit closes the reference/instrument gate only. Subsequent C–F evidence
closes production repairs, baselines, optimization and final acceptance; the
tracker records overall EX07 as `validated`.

| B requirement                  | Validation                                                      |
| ------------------------------ | --------------------------------------------------------------- |
| Independent physical oracle    | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Known-input native probes      | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Independent image reference    | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Material sampling              | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Deterministic workload recipes | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Runnable primary fixture       | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Bounded instruments            | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |
| Collection overhead            | [Evidence](validation.md#ex07b-completion-audit--task-evidence) |

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
in [the validation record](validation.md). No long benchmark or
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

## Supporting records

- [certificates](certificates.md)
- [validation](validation.md)
