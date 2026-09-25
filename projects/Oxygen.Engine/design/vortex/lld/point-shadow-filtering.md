# EX07E04 — Cube hardware PCF implementation contract

Status: implemented, qualified and visually accepted on 2026-09-25. The user
approved UE-aligned point-light hardware PCF and Low/Medium/High/Ultra counts of
**1/5/29/29**. E06 sharing subsequently completed as the last E implementation
item. The [accepted comparison report](../milestones/exposure/EX07/EX07E/validation.md) records
final quality, cost and evidence; the requirements below describe the delivered
contract.

## Source contract

The local UE5.7.4 source under `F:/Epic Games/UE_5.7/Engine` establishes:

- `ShadowSetup.cpp`, `SetupWholeSceneProjection`: cube faces look along the
  negative cube axes, and sampling uses the receiver-to-light vector.
- `ShadowDepthVertexShader.usf` and `ShadowDepthPixelShader.usf`: one-pass point
  shadows store rasterized projected depth, without the projected-spot linear
  depth override or caster depth bias.
- `ShadowProjectionCommon.ush`, `CubemapHardwarePCF`: bilinear cube comparison,
  disk offsets in the tangent plane, projected receiver depth, and a positive
  reversed-Z comparison offset `(constant_bias + slope_bias) / clip_w`, scaled by
  the disk sample radius for multi-sample filtering. The slope term here is the
  configured coefficient, not a receiver-normal slope calculation.
- `ShadowRendering.cpp`, `UpdateShaderDepthBias`: point constant coefficient
  `0.02 * 512 / resolution * 2 * user_bias`; slope coefficient is constant bias
  times `3 * 0.5` at UE's default authored slope setting.
- `BaseScalability.ini`: UE High and Epic use shadow quality 5 (29 comparisons).

## Oxygen implementation

All cube-local maps, including hemispherical spots, migrate together. Ordinary
projected spots and CSM retain their accepted depth/bias/filter contract. The
unaccepted four-gather experiment is preserved under `out/analysis/ex07e/e04-pcf`
as evidence and removed from the product path.

- Retain right-handed raster transforms and reversed-Z D32. Cube SRVs become
  `TextureCubeArray`, with the physical cube index `first_array_layer / 6`.
  Sampling uses `origin - receiver`. Physical faces look along
  `-X,+X,-Y,+Y,-Z,+Z`, with RH view up vectors `-Y,-Y,+Z,-Z,-Y,-Y`.
  This matches native cube addressing without changing raster winding.
- Cube depth writes use raster depth, without applying constant or slope bias in
  the producer. A `CUBE_SHADOW` permutation in the existing depth pass omits
  `SV_Depth` output and caster-normal/bias work. Masked coverage remains in that
  pass. Projected-spot/CSM permutations retain their depth override. Constants
  remain 128 bytes; no runtime depth-mode word or interpolator is needed.
- Keep Oxygen's existing near-plane policy `min(0.1 m, range * 0.01)`.
  Normalize UE's point coefficient by that near plane: publish clip-space bias
  `0.02 * near_plane_m * 512 / resolution * 2 * user_bias`.
  UE's cube near plane is one native unit; this scaling preserves the relative
  projection/bias relationship in Oxygen's metre-based coordinates, including
  short-range maps. Do not paste a centimetre-space clip coefficient into metres.
- Apply authored normal displacement once, before projection. Remove the old
  automatic linear-depth texel offsets from the cube receiver. Authored zero
  depth bias remains zero; this does not migrate Oxygen's authored defaults to
  UE's defaults. Apply the constant plus default slope coefficient only during
  comparison, divided by clip W. Positive offsets increase reversed-Z visibility.
- Use existing comparison sampler slot 1 (`GREATER_EQUAL`, bilinear filtering).
  Native cube sampling supplies cross-face filtering. Guard the light origin,
  near/far coverage, and tangent-basis poles explicitly. Face selection must use
  the same receiver-to-light vector as hardware sampling.
- Retain the 448-byte cube record. Replace reserved words at 440/444 with
  `pcf_sample_count` and one reserved word; update CPU/HLSL ABI tests together.
  Quality changes affect consumers, not stored depth or caster cache identity.

## Required evidence

CPU setup tests must independently verify cube addressing at asymmetric points
on every face, reversed-Z projection, near/far clipping, and bias units/scaling.
Native tests must cover actual hardware comparison, orientation, seams/poles,
nonuniform descriptors, multiple cubes, masked coverage, short/long ranges,
authored bias, and forward/deferred consumers. Preserve the caster-normal
regression on a projected shadow path where caster slope bias remains active.

Compile the ABI probe and production archive, run relevant tests in both existing
Ninja Release trees, then obtain bounded matched performance/visual comparisons.
Report the quality change explicitly: old nine point comparisons and new 29
bilinear comparisons at High are not identical filters. New baselines require
manual visual validation before commit.

## Bounded follow-up after the GPU regression

The first qualified 29-comparison Sponza candidate is slower than the prior
nine-load profile (31.606 versus 24.125 ms GPU frame interval). A bounded
specialization experiment preserves the approved PCF kernel and bias. Cooked
source inspection confirms exactly zero source radius for all 23 Sponza and
49 Instancing point lights. The deferred point entry can statically identify
point attenuation and cube shadows; an exactly-zero-radius pixel permutation
can additionally remove finite-source BRDF work. Nonzero radii, including tiny
positive values, must retain the existing finite-source path. Qualify mixed
punctual/finite draws in one frame before drawing performance conclusions.
