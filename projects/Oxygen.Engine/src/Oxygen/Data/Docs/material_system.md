# Material assets

Read: [storage](#storage), [occlusion](#occlusion), [emission](#emission),
[runtime binding](#runtime-binding), [validation](#validation).

## Storage

`MaterialAsset` exposes immutable cooked material data. `MaterialAssetDesc` in
`PakFormat_render.h` owns the packed layout; its material descriptor version is
independent of the enclosing PAK version. Version 3 occupies 363 bytes, followed
by one `ShaderReferenceDesc` per set bit in `shader_stages`, in ascending bit order.
Readers reject other descriptor versions before interpreting material fields.
Content is regenerated through the current native cooker; no older reader ships.

The descriptor stores domain, flags, scalar PBR parameters, resource-table
indices, UV transforms and procedural-grid parameters. `base_color`,
`normal_scale`, `emissive_factor`, IOR and UV/grid values use float32. Bounded
metalness, roughness, occlusion and alpha-cutoff factors use `Unorm16`. Optional
sheen and attenuation colors retain their declared binary16 representation.
The header and `static_assert` are the layout authority; do not duplicate offsets
in managed code.

Texture indices identify resources in cooked content, not GPU descriptors.
The core slots are base color, normal, metallic, roughness and ambient occlusion.
Additional slots include emissive, specular, sheen, clearcoat, transmission and
thickness. A field's presence in the descriptor does not establish shader support
for a shading model. `MaterialShadingConstants` defines the current GPU consumer.

Material flags are defined by `kMaterialFlag_*` in `PakFormat_render.h`.
Geometry visibility, casting and receiving policies belong to scene/renderable
state rather than an invented material flag convention.

## Occlusion

Ambient occlusion has an explicit texture binding. A packed metallic/roughness
texture does not imply AO in its red channel. When an authored AO binding shares
that resource, the shader reuses the metallic/roughness sample; a separate binding
samples its own red channel.

`kMaterialFlag_AmbientOcclusionStrength` tags the stored scalar as strength:
`AO = 1 + strength * (sample - 1)`. glTF occlusion textures use this mode; strength
zero is unoccluded and strength one preserves the texture. Without the flag,
Oxygen's scalar factor multiplies the sampled AO, or stands alone without a
texture. Missing or disabled texture sampling gives neutral AO in strength mode.

The flag occupies the existing flags field and changes no CPU/GPU layout. Cook
affected imports and rebuild the shader archive together. The independent
material oracle and production G-buffer raster tests cover absent, shared and
separate AO bindings and strength endpoints.

## Emission

The canonical source descriptor stores linear `parameters.emissive_color` in
[0,1] and `parameters.emissive_intensity` in [0,65504]. Both are finite float32
values. Defaults are white and zero. The cooker multiplies them once and stores
only the resulting three float32 channels in `emissive_factor`. Authoring keeps
color when intensity is zero; the cooked product does not reconstruct that split.
The factor-only authoring property is retired.

Float32 storage preserves authored HDR values without binary16 rounding, such as
9.7 becoming 9.703125. The change adds six bytes per cooked material. Runtime GPU
material constants already use float32 RGB, so their size and shader ABI remain
unchanged. This does not require higher-precision render targets or changes to
captured-sky IBL precision, filtering or publication.

Emission is self-illumination multiplied by an optional emissive texture and
added to surface shading. It does not introduce emissive global illumination.
The finite 65504 authoring limit is Oxygen's accepted working range, not a claim
that all resulting lit pixels fit binary16.

Authoring semantics and migration are owned by the
[material editor LLD](../../../../../../design/editor/lld/material-editor.md#7-data-contracts).
UE5.7's `Core/Public/Math/Color.h` uses float32 `FLinearColor`; its
`MaterialTemplate.ush` and `BasePassPixelShader.usf` evaluate and add emission
independently of reflected lighting. GPU intermediate precision remains a
separate rendering decision.

## Runtime binding

Content loads material descriptors and resolves texture dependencies. Vortex's
`MaterialBinder` maps loaded resources to bindless indices and uploads the
supported fields in `MaterialShadingConstants`. Material assets do not allocate
GPU resources or expose packed GPU state to editor clients.

Use `MaterialAsset` accessors for native observations. Default/debug material
factories return immutable assets with current descriptors. Per-instance material
slot assignment belongs to geometry/scene slot contracts, not mutable copies of
the cooked material descriptor.

## Validation

Qualify native producer/load round trips, current-version acceptance, obsolete
version rejection and finite HDR precision. Source migration preserves texture
references and representable appearance, then recooks content. Update fixture
writers with the current format; they are not compatibility readers.

Preserve the independent GPU material layout. Existing raster tests cover scalar
and textured emission alongside exposure and alpha modes; expected values use
the float32 source product without binary16 quantization.

For native visual checks, import `Cooker/Test/Import/Models/static_ao_grid.gltf`
with the `static` policy and transform baking disabled, then load its cooked
index or PAK in RenderScene. Use its authored camera and captured sky lighting.
The two rows read left to right:

| Row    | First                   | Second                    | Third                   | Fourth                |
| ------ | ----------------------- | ------------------------- | ----------------------- | --------------------- |
| Top    | No AO                   | Shared AO, strength 0     | Shared AO, strength 0.5 | Shared AO, strength 1 |
| Bottom | Separate AO, strength 0 | Separate AO, strength 0.5 | Separate AO, strength 1 | No AO reference       |

The shared texture has red 0; the separate texture has red 0.6. Expected AO is
`1, 1, 0.5, 0` above and `1, 0.8, 0.6, 1` below. Compare equal tiles under fixed
exposure; use the G-buffer raster tests for numerical AO rather than inferring
linear values from tone-mapped screenshots. The adjacent
`static_textured_triangle.gltf` and `.fbx` fixtures check color-texture import.
