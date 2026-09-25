# Lighting model and capacity decisions

### D1 — admission capacities

**Approved 2026-09-22: A — budgeted dynamic capacity. Conventional allocation
and compact-list implementation complete; broader workload qualification continues.**
Use indexed light/shadow records and grow resources within explicit
renderer-wide budgets and backend limits. Account for unique live allocations
across all views, frame generations, retained/discarded submissions and caches.
Report supported count/byte limits and requested/available quantities. Remove
the arbitrary four-point/eight-spot shadow cutoffs; atmosphere's two slots do
not cap ordinary directional lights or shadow families. The 4,096-light
qualification endpoint is not a hard scene-count limit.

Requested shadow resolution follows the explicitly selected quality profile.
The conventional local profile uses projected-size buckets, hysteresis and
fading under the authored/tier ceiling, as specified in
[the local-shadow design](shadow-local-lights.md#conventional-local-quality-and-allocation).
Resolution cannot be reduced to recover from an allocation failure. Fixed per-kind product count profiles were
not selected. Backend representation/resource limits still apply and must be
published, checked and distinguished from memory availability.

Preserve every admitted light and requested shadow. Known invalid
candidates are rejected atomically; preparation failures invalidate the affected
view without rewriting authored data. No brightest-N selection, silent truncation,
automatic unshadowing or resolution/update-rate reduction in response to
allocation pressure is allowed. Existing
valid panes and application UI remain usable under the LLD failure contract.
Unused cached resources may be retired to satisfy a budget only when fence-safe;
in-flight allocations cannot be reclaimed or counted as free.

The complete-list range representation is the correctness-preserving
response to compact-index exhaustion. It changes list encoding, not light power,
BRDF or shadow requests. Count fallback work explicitly; it cannot be reported
as culling improvement or used to waive the performance gate.

The initial 65,536-local/64-directional/64-MiB-index/1-GiB-per-view values were
unapproved engineering proposals. They are withdrawn as freeze recommendations.
In particular, a per-view allowance without a renderer-wide aggregate ceiling
is not a bounded memory contract. Choosing the policy does not approve those
numbers. EX07A must finish backend allocation accounting and freeze numeric
admission profiles before consumer cutover. EX07D separately freezes measured
CPU/GPU/memory performance budgets; it does not postpone the A capacity decision.

#### Capacity arithmetic and correction

At the target 80-byte local/64-byte directional strides, 65,536 local records
would occupy 5 MiB and 64 directionals 4 KiB. These remain illustrative arithmetic,
not a supported count claim. A 64px/32-slice grid has 16,320 cells at 1080p
(127.5 KiB ranges) and 65,280 at 4K (510 KiB). Duplicating all 4,096 indices in
all 4K cells costs 1,020 MiB; complete-list encoding avoids that duplication.

The earlier 256/1,024 MiB shadow figures counted only the 32-bit depth component,
so they must not be used as surface-allocation costs. All three current shadow
targets use `Format::kDepth32Stencil8` in
`src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.cpp`.
D3D12 maps this to `R32G8X24_TYPELESS` with `D32_FLOAT_S8X24_UINT` views in
`src/Oxygen/Graphics/Direct3D12/Detail/FormatUtils.cpp`.
The [DXGI format definition](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format)
is 64 bits, including stencil/unused components: **8 nominal bytes per texel**.

| Example request                                                             | Directional bytes | Point bytes | Spot bytes | Nominal total |
| --------------------------------------------------------------------------- | ----------------: | ----------: | ---------: | ------------: |
| 2 directionals x 4 cascades at 2048; 4 point cubes at 1024; 8 spots at 1024 |           256 MiB |     192 MiB |     64 MiB |       512 MiB |
| Same counts, directionals at 4096 and point/spot at 2048                    |         1,024 MiB |     768 MiB |    256 MiB |     2,048 MiB |

These are format/resolution calculations for one set of maps, not measured
backend allocations or total renderer memory. Query the backend's allocation
requirements for actual alignment/placement and account separately for light
buffers, list/status/scratch/contact resources and every distinct live/cached/
retired allocation. Shared or correctly ordered reused resources are counted once;
additional outstanding allocations are counted until their fences retire.
Do not assume either free frame reuse or a fixed frames-in-flight multiplier.

### D2 — physical lighting and artistic attenuation scope

**Approved 2026-09-22: A — physical model only. Implementation pending.**
Point/spot punctual lighting uses the specified inverse-square propagation,
normalized spot flux, range window and numerical guard. There are no Linear or
CustomExponent alternatives. This decision supersedes the earlier plan's
requirement to retain those controls and the initial artistic-profile proposals.
BRDF surface response and light propagation remain separate physical contracts;
see the independent [PBRT point/spot derivation](https://pbr-book.org/4ed/Light_Sources/Point_Lights).

Required strict cutover in EX07-C:

- Remove `AttenuationModel`, its point/spot getters/setters, and local
  `decay_exponent`/`DecayExponent` fields and accessors from native/script APIs,
  source/packed formats, editor persistence/transport and tooling. Do not keep
  a one-valued selector, ignored parameter, alias or compatibility branch.
- Migrate all repository producers, imported-light translation, examples,
  authored assets, fixtures, serializers, packers/dumpers and consumers together.
  New source schemas reject either obsolete field even when its value was the
  former default. Old packed layouts are rejected, not reinterpreted.
- Keep base lux/lumens, tint, per-light compensation, range and spot cone pairs.
  Forward/deferred and qualification references use the same physical model.
  Source radius follows the approved finite-emitter decision D3. Directional CSM
  `distribution_exponent` is unrelated and remains supported.
- LP16/LP17 become removal obligations: verify absent API/wire fields, obsolete
  source and packed-input rejection, migrated producer output and physical
  point/spot round-trip/rendered results. No artistic-mode tests or GPU fields
  remain in the target contract.

### D3 — local source radius

Source radius controls the analytic finite-source diffuse horizon and specular
highlight in [production model 2](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2).
Point and spot lights share that evaluator. Radius zero uses the punctual branch;
positive radius uses a spherical source cap, one specular refinement and
source-size energy normalization. Range and spot attenuation use the source
center. Bounds and ordinary projected shadows therefore do not expand with radius.

Authored flux still determines peak intensity through the point or spot
photometric normalization. The source approximation does not promise exact
sphere/disk radiometry near the emitter or at cone/range edges. Independent
numerical integration measures those differences outside production. Runtime
quadrature was rejected because its GPU cost outweighed the improvement for this
renderer. The PBR design rationale records measured quality, timing and memory.

Zero separation returns zero; the punctual 1 mm guard remains separate from
source size. The existing source-center PCF/contact visibility remains unchanged:
source radius does not introduce radius-dependent shadow penumbrae.

### D4 — common BRDF quality target

Forward, deferred and existing indirect consumers share correlated Smith GGX,
Schlick Fresnel, metallic-roughness/specular inputs and perceptual roughness floor
0.045. View-dependent energy compensation uses one 32x32 RG32F `(E,B)` texture,
hardware filtered with square-root view-cosine coordinates. There is no separate
mean texture or incident-direction moment lookup.

For each channel, `W=1+F0*(1-E)/E` scales single scattering and
`R=W*(F0*E+(1-F0)*B)` describes integrated specular response. Normalized diffuse
uses scalar `saturate(1-luminance(R))` to preserve base hue. Ambient occlusion
remains separate from direct-light visibility. Material authoring and dielectric
F0 mapping do not change.

This compensation gives up exact reciprocity to reduce lookup and arithmetic
cost. Independent energy and lobe comparisons quantify approximation quality;
ABI, finite output, light identity, supported influence and forward/deferred
consistency remain implementation checks. Direct and indirect migrate together,
with model revision 2 and no shipping compatibility evaluator. See the
[PBR tradeoffs](../../renderer-core/physically-based-rendering.md#design-tradeoffs-and-rejected-alternatives)
for the alternatives and measured errors.

### D5 — hemispherical spot support

Accept `0 <= inner < outer <= pi/2`, including the 90-degree soft endpoint
supported by glTF. Equal-angle hard cones remain supported below 90 degrees.
Ordinary spots use cone proxies and one projected shadow, including when source
radius is nonzero. Only the hemispherical endpoint uses the existing spherical
proxy and cube/multiple-face shadow route because a single perspective map cannot
cover its 180-degree field of view. Budget all six faces and retain explicit
per-light projection metadata. Do not clamp valid imported wide cones.

GPU publication requires representable outer cosine and, for soft cones, a
positive representable cosine width. CPU photometric normalization uses both
authored angles in double precision; GPU angular shading uses the FP32 squared
center-cone ramp. This replaces compensated per-pixel cone arithmetic.

### D6 — default resource envelope

**Approved 2026-09-22: A — 4 GiB total / 128 MiB compact indices.** A standalone D3D12 query on the reference
RTX 3080 (10 GiB, driver 610.62) now confirms allocation requirements for the
current typeless D32S8 resource descriptions. No shadow resources were allocated
and no rendering/performance claim follows from this probe. See
[allocation requirements](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements-current.json).

The queried medium narrow-spot set is 512 MiB; using six-face maps for all eight
spots makes it 832 MiB. Corresponding maximum-resolution sets are 2,048 and
3,328 MiB. Two distinct medium wide-spot view sets cost 1,664 MiB; two overlapping
generations of those sets cost 3,328 MiB, before other lighting resources.
These are conservative separate-map examples, not a requirement to duplicate
view-independent maps. The renderer must charge actual unique allocations.

The subsequent [shadow-memory review](../milestones/exposure/EX07/EX07E/shadow-memory.md) validates
stencil ownership and selects depth-only conventional maps, compatible same-frame
local-map sharing and bounded allocator reuse/growth. The figures above remain
the queried **current D32S8 baseline**, not the optimized target. Native large-map
allocation queries halve those map sizes for D32; the GPU format probe matches
depth/PCF results. Production cutover and timing remain unqualified. Scene/custom
stencil and VSM products retain their own owners. D6's ceilings are unchanged;
they do not establish a comfortable whole-engine working set on the 10 GiB GPU.

Approved configurable defaults, not preallocations:

| Profile                  | Renderer-wide lighting/shadow ceiling | Aggregate compact-index sublimit, included in the ceiling | Tradeoff                                                                                                                                                                                                                   |
| ------------------------ | ------------------------------------: | --------------------------------------------------------: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Approved default profile |                                 4 GiB |                                                   128 MiB | Covers the queried two-view medium map replacement example with remaining room for other lighting products, or one maximum wide-spot set. Higher allowed memory use, bounded by the driver/renderer allocation budget too. |

The ceiling covers lighting-owned records, grids/lists/scratch/status, BRDF data,
shadow/contact products and distinct cached/retired generations. Charge backend
allocation sizes, not just authored payload bytes. Shared backing/heap usage
must be accounted once at the owning allocator and bounded by the renderer's
parent memory budget; do not ignore heap slack or charge one shared heap to each
view. Driver-budget availability remains an additional admission check, not a
reservation or a guarantee. A smaller device/application may explicitly configure
a different ceiling; requested and effective availability are caller-visible.

Compact-index exhaustion uses complete-list encoding while required products
still fit the total ceiling. The sublimit grants no permission to drop lights.
No scene-light count is invented from these examples: publish checked backend/
representation bounds and the candidate's complete required/available byte costs.
The approved correctness/performance workloads remain required; these budgets
do not qualify them or waive any lifecycle/performance gate.

These decisions concern supported fields, their physical meaning and capacity.
D2 explicitly removes two previously retained controls; all other retained
fields still require real consumers. Scene-owned directional authority,
physical units and the approved performance workload remain unchanged.
