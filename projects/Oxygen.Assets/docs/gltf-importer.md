
# glTF 2.0 importer (Oxygen.Assets) — design & strategy

> Scope: This document defines the intended *behavioral contract* of the Oxygen glTF 2.0 importer: what it must extract from glTF, how it maps to Oxygen authoring assets and cooked/runtime formats, what it validates, and what decisions remain open.

## Background / current state

Oxygen.Assets currently contains an MVP importer that:

- Loads `.glb` and `.gltf` via SharpGLTF.
- For `.gltf`, materializes external `buffers[].uri` and `images[].uri` into a protected temp workspace (path traversal/scheme checks).
- Generates authoring assets:
  - Textures: `.otex.json` + extracted intermediate PNGs.
  - Materials: `.omat.json` (merging if a file already exists).
  - Geometry: `.ogeo.json` plus intermediate `.glb` export.
  - Scene: `.oscene.json` (a minimal TRS node tree).
- Cooking:
  - Geometry: emits cooked buffers (`buffers.table`/`.data`) and `.ogeo` descriptors.
  - Textures: emits `textures.table`/`.data`.
  - Materials: emits `.omat` descriptors **but currently does not emit texture bindings** (indices are written as 0 regardless of actual textures).
  - Scenes: currently writes the authoring JSON as “cooked” (placeholder).

**Decision (2026-01-01):** For scenes imported from `.glb`/`.gltf`, the importer produces *only* the authoring JSON source file (`.oscene.json`). Scene cooking is performed only for scenes authored in the editor.

This doc designs the importer “for real” against:

- glTF 2.0 spec requirements (coordinate system, accessors/stride/normalized/sparse, PBR texture color spaces, tangent/normal rules, skins/animations/morph targets).
- The existing Oxygen runtime contracts in `PakFormat.h` and the current cook pipeline (resource tables + per-asset binary descriptors).

## Goals

- Deterministic, debuggable, and safe import from `.glb` and `.gltf` (including external resources).
- Correct interpretation of glTF binary data (including `byteStride`, `normalized`, and `sparse`).
- Clear mapping from glTF constructs → Oxygen assets and cooked descriptors.
- Stable asset identity and regeneration strategy that preserves user edits where intended.
- Explicit extension handling policy (supported / ignored / fatal).

## Non-goals (for the importer itself)

- Not a full DCC replacement: importer will not attempt to “fix” arbitrary invalid glTF beyond spec-defined defaults.
- Not a renderer: the importer does not perform shading or BRDF *evaluation*.
- However, **BRDF/PBR is in-scope for import**: the importer must preserve and faithfully map all material parameters required to implement a correct glTF PBR BRDF later (even if the engine’s runtime BRDF/shader pipeline is still evolving).
- Not a general-purpose scene editing system.

## Glossary / conceptual mapping

- glTF “asset”: the top-level JSON + binary payload(s).
- glTF “scene”: a list of root nodes.
- glTF “node”: hierarchy + local transform (TRS or decomposable matrix) and optional references (mesh, camera, skin, weights).
- glTF “mesh”: array of primitives.
- glTF “primitive”: a draw call: attributes + optional indices + material + topology mode.
- glTF “accessor”: typed view into binary (component type + shape + count + normalization + sparse overrides).
- glTF “image/texture/sampler”: image pixel data + sampler state, referenced by materials.

Oxygen assets (today):

- `Texture` authoring (`.otex.json`) + cooked texture resources (`textures.table/.data`).
- `Material` authoring (`.omat.json`) + cooked material descriptor (`.omat`).
- `Geometry` authoring (`.ogeo.json`) + cooked geometry descriptor (`.ogeo`) + cooked buffers resources (`buffers.table/.data`).
- `Scene` authoring (`.oscene.json`) + cooked scene descriptor (currently missing; runtime expects binary scene).

## Coordinate system, units, and handedness

glTF 2.0 coordinate system is right-handed, +Y up, and linear distances are in meters; angles are radians.

Oxygen engine coordinate-space conventions are defined in `Oxygen.Engine/src/Oxygen/Core/Constants.h` and are not configurable:

- Right-handed
- Z-up
- Forward = -Y
- Right = +X

These do **not** match glTF’s +Y-up convention, therefore the importer must apply a consistent basis conversion from glTF space into Oxygen engine world space.

**Importer rule:** A global axis-conversion **is applied** for glTF→Oxygen so that imported scenes and cooked geometry obey the engine’s coordinate-space law. The importer must still:

- Preserve and correctly interpret node TRS.
- Handle negative scales (mirroring) and the implied winding-order consequences.
- Treat units as meters end-to-end.

Open question: if Oxygen ever needs “engine units” different from meters, introduce an explicit `importScale` (default 1.0) rather than baking ad-hoc axis conversions.

## Import architecture (phases)

The importer is conceptually split into four phases.

### Phase 0 — Input safety & workspace materialization

For `.gltf`, external URIs for `buffers[].uri` and `images[].uri`:

- MUST be treated as *relative* filesystem references only.
- MUST reject absolute paths, drive-letter paths, schemes (`http:`, `data:`), and path traversal.
- MUST be copied into an isolated temp folder, maintaining relative structure.

For `.glb`, embedded resources are used directly.

Output: a resolved “workspace” containing a single `.gltf` (or extracted `.gltf` view of `.glb`) plus referenced files, all inside the temp root.

### Phase 1 — Parse & validate (structural)

The importer validates:

- JSON structure and indices (all referenced indices in bounds).
- That node transforms are valid per spec:
  - A node MAY specify `matrix` OR TRS; not both.
  - If `matrix` is present, it MUST be decomposable into TRS.
  - If node is animation target, `matrix` MUST NOT be present.
- Accessor alignment/shape requirements (e.g., indices accessor is `SCALAR` with unsigned int type; attribute semantic allowed type).

Extension policy (see below) is enforced here.

### Phase 2 — Normalize (semantic)

This phase constructs **engine-compatible** internal representations.

The output of Phase 2 is the canonical data that later feeds both authoring JSON and the cook pipeline; therefore it must already obey the engine’s coordinate-space conventions and the renderer’s data layout expectations. If Phase 2 is wrong, downstream systems will fail in subtle ways (wrong winding/culling, flipped normals, broken normal maps, invalid bounds, unstable skinning) that are expensive to debug.

Key normalization responsibilities (detailed):

1) **Decode accessors correctly (byte-accurate)**

- Decode accessors with full support for `bufferView.byteStride`, `accessor.normalized`, and `accessor.sparse`.
- Produce tightly-packed, typed arrays that match Oxygen’s runtime/cook expectations (e.g., float3 positions/normals, float2 uvs, u16/u32 indices).

1) **Convert from glTF space to Oxygen engine world space**

Oxygen engine space is right-handed, Z-up, Forward = -Y, Right = +X (see `Oxygen.Engine/src/Oxygen/Core/Constants.h`). glTF is right-handed and +Y-up. Normalize by applying a consistent basis change with **positive determinant** (preserves handedness) such as:

- +X (glTF) → +X (Oxygen)
- +Y (glTF, up) → +Z (Oxygen, up)
- +Z (glTF) → -Y (Oxygen, forward)

This basis conversion must be applied consistently to all spatial data:

- Node local transforms (TRS or decomposed matrix)
- Vertex positions and all direction vectors (normals/tangents/bitangents)
- Skin inverse bind matrices and joint transforms
- Camera transforms (and later light transforms)
- Computed bounds (AABBs / spheres)

1) **Make geometry render-correct under engine conventions**

- Ensure triangle front faces match the engine’s clip-space contract (CCW front face in `Oxygen.Engine/src/Oxygen/Core/Constants.h`).
- Handle negative-scale (mirrored) transforms:
  - Detect when a node’s world transform flips winding.
  - Ensure either the geometry winding, culling mode, or a per-instance flag is adjusted so the object renders consistently.

1) **Guarantee a valid tangent basis for normal mapping**

- If `NORMAL` is missing: compute **flat normals** (glTF default behavior), and treat any provided tangents as invalid.
- If tangents are missing but normal mapping is used (or required by the engine’s shading path): generate MikkTSpace tangents.
- Derive bitangent consistently: `bitangent = cross(normal.xyz, tangent.xyz) * tangent.w`.
- Preserve the glTF normal map convention (tangent-space, OpenGL +Y / green-up). This must align with Oxygen’s normal decode and with the generated tangent basis.

1) **Normalize skinning data for stable animation**

- Decode `JOINTS_0` / `WEIGHTS_0` correctly (including normalized integer decoding).
- Clamp weights to non-negative and renormalize after dequantization so the sum is 1 (within an epsilon).
- Validate joint indices are in-range for the referenced skin.

1) **Normalize animation curves to runtime-safe form**

- Ensure strictly increasing keyframe times per sampler.
- Normalize quaternions after interpolation (and after basis conversion) to avoid drift.
- For CUBICSPLINE, preserve tangents correctly (do not reinterpret as LINEAR).

1) **Produce engine-compatible vertex streams and bounds**

- Emit vertex streams that match the engine’s expected cooked layout (currently a fixed 72-byte vertex format elsewhere in this doc).
- Compute bounds from the converted, final vertex positions (engine world basis), not from raw glTF data, so culling and picking are correct.

### Phase 3 — Emit Oxygen authoring assets (+ dependencies)

Emit “source-of-truth” authoring assets in the destination asset folder:

- `.otex.json` + extracted/converted image sources
- `.omat.json`
- `.ogeo.json` (+ any intermediate geometry payload if still needed)
- `.oscene.json`

**Design principle:** authoring assets should be inspectable/diffable, but should not be forced to mirror glTF’s internal structure.

### Phase 4 — Cook

Cooking produces runtime descriptors and shared resource tables.

**Decision:** The glTF importer must not cook scenes. It only emits `.oscene.json` authoring source. Any scene cooking (including producing `.oscene` runtime payloads) happens exclusively for scenes authored/saved in the editor.

## Stable identity & naming

This importer must follow Oxygen’s existing identity rules:

- **Asset identity** is a stable GUID: `oxygen::data::AssetKey` (engine/runtime) / `AssetKey` (Oxygen.Assets).
- **Editor-facing addressing** is by **VirtualPath** (e.g. `/Content/Materials/Wood.omat`).
- For import, **AssetKey ownership is persisted in the source sidecar** (`<SourcePath>.import.json`) by `SidecarAssetIdentityPolicy` and is keyed by `VirtualPath`.
- **Runtime resources** (buffers/textures tables) are not identified by AssetKey; they are uniquely identified at runtime by `(SourceKey, resource_type, resource_index)` and cached via `ResourceKey`. The importer must not attempt to mint or persist `ResourceKey`s.

### Requirements

- Deterministic output **VirtualPaths** for all extracted assets given the same input + settings.
- Reuse AssetKeys across reimport by using `context.Identity.GetOrCreateAssetKey(virtualPath, assetType)` (sidecar-backed).
- Do not invent a parallel identifier scheme in generated authoring JSON. The stable identity is `AssetKey`; the stable address for tooling is `VirtualPath`.
- Reorder-resilient when possible: if a glTF exporter reorders arrays between exports, the importer should strive to keep previously generated VirtualPaths stable by consulting the parent sidecar’s prior outputs and matching to the same logical sub-assets.

### Output VirtualPaths (Oxygen.Assets convention)

Oxygen import produces **VirtualPaths** (and thus AssetKeys) for each extracted asset. For composite sources like `.glb`/`.gltf`, the parent source sidecar stores the AssetKeys for *all* generated outputs (generated sources do not have their own sidecars).

Current convention (matches the existing `GltfImporter` behavior):

- Scene: `/<dir>/<stem>__scene.oscene`
- Geometry: `/<dir>/<stem>__mesh__0000.ogeo` (one per glTF mesh)
- Material: `/<materialDir>/<stem>__material__0000.omat`
- Texture: `/<dir>/<stem>__texture__0000.otex`

Where:

- `<stem>` is derived from the source filename.
- `<dir>` is derived from the import destination (typically the source directory, unless overridden by settings).
- `<materialDir>` may be overridden by importer settings (e.g., `MaterialDestination`).

**Important:** In Oxygen, “stable identity” comes from the sidecar mapping of `VirtualPath -> AssetKey`. Therefore, changing VirtualPaths is a breaking identity change for reimport and references.

### Regeneration / merge policy

Some assets (materials especially) may be edited by users after import.

Importer must support a “merge” mode:

- For materials: if the generated `*.omat.json` already exists, preserve user edits by reading and reusing the existing JSON as the source of truth (then only fill in missing generated fields).
- For geometry metadata: preserve user-assigned per-submesh material assignments when reimporting (current MVP behavior).
- Keep the parent sidecar’s `VirtualPath -> AssetKey` bindings stable: if the VirtualPath is reused, the AssetKey must be reused.

Note: A per-property “generated” marker can be added later if/when material authoring becomes richer, but it must be consistent with the existing authoring JSON schemas and sidecar strictness rules.

## Extension policy

glTF exposes `extensionsUsed` and `extensionsRequired`.

Importer must:

- Fail import if any extension in `extensionsRequired` is not supported.
- For `extensionsUsed`:
  - If supported: apply.
  - If unsupported: warn and ignore (unless it materially affects geometry decoding/material meaning, in which case consider “fail in strict mode”).

### Target support tiers (game engine “Blender parity” priorities)

For Oxygen, “we see what Blender sees” must be interpreted through a **real-time engine** lens:

- Prioritize extensions that change *material meaning* and *UV mapping*, because those are the biggest contributors to “looks different in engine than Blender”.
- Treat mesh compression and exotic workflows as separate tiers: they matter for ingest/perf and compatibility, but they do not change shading semantics once decoded.

#### Tier 0 — Core correctness (must-have)

- Core glTF 2.0 geometry + PBR metallic-roughness + node TRS.
- `KHR_mesh_quantization` (geometry correctness): required to decode common quantized assets the same way Blender does.

#### Tier 1 — Visual parity essentials (high impact for game engines)

These are the extensions most likely to affect what you see in Blender vs what you see in Oxygen’s editor viewport:

- `KHR_texture_transform` (UV scale/offset/rotation per binding).
- `KHR_materials_unlit` (authoring intent: “shaded off”).
- `KHR_materials_emissive_strength` (emissive intensity; common in modern pipelines).

#### Tier 2 — Visual parity materials (common “looks wrong” cases)

Support these if we want the editor viewport to match Blender for typical modern assets:

- `KHR_materials_clearcoat` (automotive clear coat).
- `KHR_materials_transmission` + `KHR_materials_volume` (glass/liquids).
- `KHR_materials_sheen` (cloth/fabric).
- `KHR_materials_specular` + `KHR_materials_ior` (controls dielectric specular/IOR; important for matching Blender’s Principled response).

#### Tier 3 — Ingest/performance compatibility (decode-only)

- `EXT_meshopt_compression`
- `KHR_draco_mesh_compression`

These extensions primarily change how data is stored on disk; once decoded, the runtime geometry is the same. If unsupported, we should fail import in strict mode (and warn clearly in permissive mode), because partial decoding is worse than a hard failure.

#### Tier 4 — Optional authoring workflows

- `KHR_lights_punctual` (useful for previewing scenes, but not required to match material appearance).
- `KHR_materials_variants` (tooling/authoring; not required for baseline parity).

#### Decision (2026-01-01): Target support is Tier 2 end-to-end

Oxygen’s glTF import target is **Tier 0 + Tier 1 + Tier 2** supported **end-to-end**:

- Importer parses and preserves Tier 2 semantics in authoring assets.
- Cook emits runtime descriptors/resources that can represent Tier 2 parameters.
- Editor viewport and engine runtime use those parameters for rendering (i.e., Tier 2 is not “parsed then dropped”).

Compression extensions (Tier 3) remain a separate compatibility goal and do not change the Tier 2 “material meaning” target.

## Geometry extraction (meshes/primitives)

This section specifies how glTF meshes/primitives are normalized into Oxygen
geometry assets.

At a high level:

- Import produces one `.ogeo.json` per glTF `mesh` (with one submesh per glTF
  primitive).
- Cook emits one cooked geometry descriptor (`.ogeo`) plus shared vertex/index
  buffers in the buffers resource table.
- Submeshes reference materials by `AssetKey` (not by glTF indices), so
  material identity remains stable across reimport.

## What is computed where (pipeline vs runtime vs shaders)

This section makes the division of responsibility explicit. This split is consistent with how most real-time engines structure their asset flow:

- **Asset pipeline (import + cook)** performs expensive, deterministic, content-dependent work once, so runtime load is fast and repeatable.
- **Runtime loading** resolves references, allocates GPU resources, and builds per-frame/per-draw binding data from cooked descriptors.
- **Shaders** evaluate the BRDF and do per-pixel math (texture sampling, normal mapping, lighting), using data prepared by the first two steps.

### Asset pipeline (importer + cook)

Compute/author here (one-time, deterministic):

- **Decode & normalize geometry** (accessors/stride/normalized/sparse) and produce engine-ready vertex/index streams.
- **Basis conversion** (glTF +Y up → Oxygen Z up; preserve RH) applied consistently to vertex data and node transforms.
- **Generate missing tangent space** (MikkTSpace) when normal mapping is required and tangents are absent.
- **Texture processing**: extract embedded images, transcode/convert to the engine’s preferred intermediate inputs for the texture cooker.
- **Material semantics preservation**: write complete authoring material JSON capturing glTF meaning (including extension semantics).
- **Cooked descriptor emission**:
  - Produce binary descriptors (e.g., `MaterialAssetDesc`) that the runtime can load without reinterpreting glTF.
  - Produce resource tables (textures/buffers) and any platform-ready payloads.

Tier 2 implication: “supported end-to-end” means Tier 2 parameters must survive this step into cooked descriptors (either by extending `MaterialAssetDesc`/shader constants or by defining a stable convention using reserved bytes/slots).

### Runtime loading (engine/editor)

Compute here (fast, per-load / per-frame), using cooked data:

- **Resolve cooked references into runtime cache keys**: the loader maps authored indices in cooked descriptors to runtime `(SourceKey, type, index)` resources and produces `ResourceKey`s.
- **Allocate GPU bindings**: a renderer-side binder turns resolved `ResourceKey`s into bindless heap indices (or other API-specific handles).
- **Build shader-visible constant snapshots** per material/draw: pack scalars, flags, UV transforms, and bindless indices into a structure mirrored by HLSL.

This aligns with the current Oxygen implementation:

- Cooked material fields live in `oxygen::data::pak::v1::MaterialAssetDesc` (scalars + 5 texture indices + reserved slots).
- The loader assigns runtime `ResourceKey`s for those texture indices when publishing a `MaterialAsset`.
- The renderer’s `MaterialBinder` resolves those keys to bindless indices and writes `engine::MaterialConstants`.

### Shader evaluation (GPU)

Compute here (per-pixel / per-vertex), using bound resources and constants:

- Sample base color (sRGB decode), normal maps (tangent-space decode), and scalar maps.
- Evaluate the BRDF (GGX/Schlick/Smith) and lighting.
- Apply UV transforms as provided (per-binding 2×3 affine matrix per the `KHR_texture_transform` strategy below).

Tier 2 implication: extensions like clearcoat/transmission/volume/sheen/specular/ior are primarily **shader features**, but they still require:

- **Descriptor representation** (cooked + runtime constants) for their parameters/textures, and
- **Shader implementation** that consumes those parameters.

### Supported topology

glTF `mesh.primitive.mode` supports points/lines/triangles variants.

**Default strategy:** import `TRIANGLES` (mode 4). For other modes:

- Either reject with a clear diagnostic (preferred initially), or
- Convert to triangles if meaningful (strips/fans) (later enhancement).

### Attribute decoding rules

The importer must support the core attribute semantics:

- `POSITION` (required)
- `NORMAL` (optional)
- `TANGENT` (optional)
- `TEXCOORD_0` (optional; additional `TEXCOORD_n` are optional to support)
- `COLOR_0` (optional)
- `JOINTS_0` / `WEIGHTS_0` (required when node uses skin)

Key glTF rules to enforce:

- `POSITION` min/max must exist in glTF; use it to compute bounds.
- If normals are missing: compute **flat normals** (spec requirement), and ignore provided tangents.
- If tangents are missing: SHOULD compute tangents using MikkTSpace when normal mapping requires it.
- Bitangent is derived: `bitangent = cross(normal.xyz, tangent.xyz) * tangent.w`.

### Accessor decoding (MUST be correct)

Even if SharpGLTF provides “high-level” access, Oxygen’s correctness requirements are:

- Respect `bufferView.byteStride` for vertex attributes.
- Respect `accessor.normalized` for integer attributes.
- Support sparse accessors (`accessor.sparse`) for vertex attributes and animation/morph data.

Note: if SharpGLTF guarantees these semantics, the importer can rely on it; but we must test and document that assumption.

### Vertex format mapping

Current cooked geometry uses a fixed 72-byte vertex layout (position, normal, uv, tangent, bitangent, color).

Importer mapping:

- Position: float3
- Normal: float3 (computed if missing)
- UV0: float2 (0 if missing)
- Tangent: float3 + handedness sign
- Bitangent: computed as above
- Color: float4 (1,1,1,1) if missing; if `COLOR_0` is VEC3 then alpha=1.0

Open decisions:

- Support multiple UV sets (`TEXCOORD_1+`) and additional vertex colors (`COLOR_1+`) — engine vertex format would need to evolve.
- Support quantized/packed vertex output (requires cooked buffer format changes).

### Index decoding

- If indices accessor is missing: primitive is non-indexed.
- If present: must be `SCALAR` and component type is UNSIGNED_BYTE/UNSIGNED_SHORT/UNSIGNED_INT.
- Cooker currently writes indices as 32-bit; importer should upcast to u32.

### Primitive/material partitioning

glTF primitives already represent partitions by material.

Oxygen geometry descriptor should preserve primitive boundaries as submeshes (material slots), so a renderable can reference geometry + material per submesh.

Open decision: do we model “one GeometryAsset per glTF mesh” or “one GeometryAsset per primitive/submesh”? Current system appears to prefer a geometry with submeshes.

## Texture & image extraction

### glTF image sources

An image can come from:

- `image.uri` (external file or embedded base64 data URI)
- `image.bufferView` + `image.mimeType`

Importer must support both. (If data URIs are currently rejected by safety logic for `.gltf`, we must still support them for *embedded image URIs* — they are part of spec. If we intentionally disallow them, that must be an explicit limitation.)

Open decision: allow `data:` image URIs? If disallowed, importer will be incompatible with “embedded glTF” usage.

### Color space semantics (material-driven)

glTF defines which textures are sRGB vs linear:

- Base color: sRGB
- Emissive: sRGB
- Metallic-roughness: linear (roughness in G, metallic in B)
- Normal: linear
- Occlusion: linear (occlusion in R)

Importer must carry this into Oxygen texture import metadata (or material metadata), so the cooker/runtime samples correctly.

Open decision: Where does the sRGB/linear flag live in Oxygen — on Texture, on the material’s binding, or in the shader permutation?

### Samplers

glTF samplers specify filtering and wrapping. Oxygen needs an equivalent.

Importer should:

- Map sampler wrap/filter to an engine sampler object (if present) or encode it in material bindings.
- Ensure default sampler semantics when `sampler` is absent (glTF defaults apply).

Open decision: Does Oxygen’s runtime have a per-texture sampler state, per-material sampler state, or fixed samplers?

### Embedded texture handling (lessons from other importers)

Blender and Godot both expose “extract embedded textures” vs “keep embedded” workflows.

Recommended Oxygen behavior:

- Default: extract embedded images to deterministic external source files in the asset folder (for diffability and separate texture-cooker control).
- Optionally: keep as embedded bytes if we want a single-file pipeline.

## Material extraction (PBR metallic-roughness)

### Core glTF material properties to map

- `pbrMetallicRoughness.baseColorFactor` (RGBA)
- `pbrMetallicRoughness.baseColorTexture` (index + texCoord + optional transform via `KHR_texture_transform`)
- `pbrMetallicRoughness.metallicFactor`
- `pbrMetallicRoughness.roughnessFactor`
- `pbrMetallicRoughness.metallicRoughnessTexture` (index + texCoord + transform)
- `normalTexture` (index + texCoord + scale)
- `occlusionTexture` (index + texCoord + strength)
- `emissiveFactor`
- `emissiveTexture`
- `alphaMode` + `alphaCutoff`
- `doubleSided`

Additionally, support `KHR_materials_unlit` as a separate material mode.

### BRDF considerations (in-scope)

glTF’s metallic-roughness material model is designed to be rendered with a physically based BRDF (commonly GGX microfacet + Schlick Fresnel + Smith masking-shadowing, with energy-conserving diffuse).

Even if Oxygen’s runtime does not yet fully implement the BRDF, the importer must treat BRDF correctness as a first-class requirement by:

- Preserving the full parameterization from glTF (factors, textures, scalar modifiers like normal scale / occlusion strength).
- Preserving intended color space (sRGB vs linear) per texture usage.
- Avoiding destructive “bakes” at import time that would prevent future BRDF correctness (e.g., collapsing workflows, discarding channels, or baking lighting).

Notes/lessons to carry forward (practical importer behavior):

- Blender’s glTF pipeline conceptually maps glTF PBR into its Principled BSDF model; treat this as evidence that glTF import should remain **renderer-agnostic** while keeping the semantic meaning intact.
- Godot’s glTF import pipeline expects to reconstruct materials suitable for its PBR shading path; this reinforces that importer output should be sufficient to drive a real-time BRDF without guessing.

Design implication for Oxygen:

- The authoring material (`.omat.json`) should capture glTF semantics in a way that can feed a future BRDF implementation without re-importing. If the engine currently supports only a subset, the cooker/runtime can choose conservative fallbacks, but the authoring data should remain complete.

#### Decision (2026-01-01): “We see what Blender sees” (Blender parity contract)

**Import contract:** When a glTF is imported into Blender and rendered using Blender’s glTF material mapping (Principled BSDF) under comparable lighting and color management, Oxygen should render the same material response. Deviations are considered bugs in the importer/cooker/runtime pipeline, not “art differences”.

This contract implies we must standardize on the same **glTF reference-style metallic-roughness conventions** that Blender expects from glTF:

- **BRDF family**: GGX microfacet specular + Schlick Fresnel + Smith masking-shadowing + energy-conserving diffuse.
- **Roughness mapping**: glTF roughness is *perceptual* roughness $r \in [0,1]$. Convert to the microfacet distribution parameter with $\alpha = r^2$ (and use $\alpha^2$ where required by the NDF implementation).
- **Dielectric $F_0$ baseline**: default dielectric $F_0 = 0.04$ (approximately IOR 1.5). Metals tint $F_0$ by base color.
- **Metalness semantics**:
  - For dielectrics (metallic = 0): baseColor is diffuse albedo.
  - For metals (metallic = 1): baseColor is specular $F_0$ tint and diffuse is (near) zero.
- **Normal mapping convention**: glTF tangent-space normal maps use the OpenGL convention (green is +Y). Tangent basis must be consistent (MikkTSpace when generated).

To make “we see what Blender sees” practical (not theoretical), the renderer must also match the ecosystem assumptions:

- **IBL response**: Implement specular IBL using the standard split-sum approximation (prefiltered environment map + BRDF integration LUT) consistent with the BRDF above.
- **Color management / tone mapping**: Blender uses a well-defined view transform (e.g., Filmic/AgX depending on Blender version and config). Oxygen must apply an equivalent view transform (or an explicitly compatible one) in the editor viewport and runtime when validating Blender parity.

#### Impact assessment (importer, editor, runtime)

This section documents what must change or be guaranteed across the pipeline to satisfy the contract.

##### Importer impact (Oxygen.Assets)

- Import remains **semantic**: no BRDF evaluation or baking, but it must preserve all glTF PBR inputs and modifiers.
- Authoring material (`.omat.json`) must capture:
  - Factors: baseColorFactor, metallicFactor, roughnessFactor, emissiveFactor.
  - Texture bindings and modifiers: baseColorTexture, metallicRoughnessTexture, normalTexture.scale, occlusionTexture.strength, emissiveTexture.
  - Alpha mode and cutoffs: alphaMode + alphaCutoff.
  - doubleSided and unlit (`KHR_materials_unlit`).
- Texture color-space intent must be preserved end-to-end:
  - baseColor/emissive are authored as sRGB; normal/occlusion/metallic-roughness are linear.
- Tangents/bitangents must match Blender expectations:
  - If tangents are generated: generate MikkTSpace.
  - Ensure glTF +Y normal map convention is not flipped.

##### Editor impact (Oxygen.Editor)

- The editor viewport must use the **same PBR shading path** as runtime (or a deliberately compatible path) when validating “Blender parity”.
- Material UI/inspection should surface the imported glTF semantics so artists can debug mismatches (factors, texture bindings, alpha mode, double-sided, unlit).

##### Engine runtime impact (PAKFormat + draw items + shading architecture)

###### Material descriptor (PAK v1)

- Cooked materials are represented by `oxygen::data::pak::v1::MaterialAssetDesc`
  (see `PakFormat.h`). The current PAK v1 descriptor already carries Tier 2
  parameters and texture slots:
  - Scalars:
    - `base_color[4]` (float4)
    - `normal_scale` (float)
    - `metalness`, `roughness`, `ambient_occlusion` (`Unorm16`)
    - `alpha_cutoff`, `specular_factor`, `clearcoat_factor`,
      `clearcoat_roughness`, `transmission_factor`, `thickness_factor`
      (`Unorm16`)
    - `ior`, `attenuation_distance` (float)
    - `emissive_factor[3]`, `sheen_color_factor[3]`, `attenuation_color[3]`
      (`HalfFloat`)
  - Texture indices (into `TextureResourceTable`):
    - Core: `base_color_texture`, `normal_texture`, `metallic_texture`,
      `roughness_texture`, `ambient_occlusion_texture`
    - Tier 1/2: `emissive_texture`, `specular_texture`, `sheen_color_texture`,
      `clearcoat_texture`, `clearcoat_normal_texture`,
      `transmission_texture`, `thickness_texture`
  - Flags:
    - `kMaterialFlag_NoTextureSampling`
    - `kMaterialFlag_GltfOrmPacked` (see ORM packing below)
    - `kMaterialFlag_DoubleSided`, `kMaterialFlag_AlphaTest`,
      `kMaterialFlag_Unlit`

This means Tier 2 “semantic preservation” is primarily an importer/cooker task:
populate these fields correctly and preserve any remaining Tier 2 semantics in
authoring JSON even if runtime shading is still catching up.

###### Draw item / submission data

- Geometry submeshes bind materials by asset key (`SubMeshDesc.material_asset_key` in `PakFormat.h`). ScenePrep resolves per-submesh materials and publishes one draw record per mesh view.
- ScenePrep’s collection output is `oxygen::engine::sceneprep::RenderItemData` (one per visible submesh). It carries:
  - `material` as a renderer-facing `MaterialRef` (provenance + resolved payload), and
  - `material_handle` as the stable handle used for bindless material constants.
- GPU draw submission uses `oxygen::engine::DrawMetadata` (64 bytes) with `material_handle` referencing a slot in the bindless material constants buffer.
- `oxygen::engine::MaterialConstants` is the shader-visible snapshot; it currently contains baseColor/metalness/roughness/normalScale/AO, texture bindless indices, `flags`, and a **UV scale/offset** pair.

Blender parity implications:

- Pass routing must respect imported alpha modes:
  - `MASK` must route to a masked/alpha-test domain (cutoff).
  - `BLEND` must route to alpha-blended domain (sorting/blending).
- glTF’s **metallic-roughness packing** must be honored. glTF’s `metallicRoughnessTexture` conventionally stores roughness in **G** and metallic in **B** (and often shares an ORM texture with AO in R).
  - Import/cook must set `kMaterialFlag_GltfOrmPacked` when binding a glTF-style
    ORM texture so shaders sample channels using the glTF convention.
- `KHR_texture_transform`:
  - Current runtime constants expose scale/offset only. Rotation support requires additional runtime representation (or a defined limitation with documented mismatch vs Blender).

###### Shading architecture

- The renderer uses a bindless descriptor heap and a bindless `StructuredBuffer<MaterialConstants>` (slot published per-frame in SceneConstants).
- The current PBR shader path includes GGX/Schlick/Smith and uses $\alpha = r^2$, and uses a dielectric baseline $F_0 = 0.04$ (good alignment with the chosen convention).
- However, the current ambient/specular term is explicitly **not real IBL** (a placeholder). Blender parity requires adding a real IBL path consistent with the chosen BRDF (prefiltered env + BRDF LUT) and using Blender-compatible color management/tone mapping during validation.

### Texture indices and the “index 0 reserved” rule

Oxygen’s cooked texture table reserves index 0 as a fallback texture.

This introduces an ambiguity:

- “No texture bound” vs
- “Use fallback texture at index 0”.

The engine includes a material flag (e.g., `kMaterialFlag_NoTextureSampling`) to distinguish “don’t sample” from “sample fallback.”

**Importer/cooker requirement:**

- If a material has no textures at all, set the “no texture sampling” flag.
- If a specific slot texture is missing but the shader expects sampling, bind 0 to get fallback.

This is a correctness fix relative to the current MVP material cooking behavior.

### Cooking strategy: assigning texture indices deterministically

The core failure mode in the current MVP pipeline is “material descriptors are
emitted before texture indices are known”. The fix is structural: cook must
establish a stable mapping from referenced textures to texture-table indices
before writing `MaterialAssetDesc`.

Required strategy (deterministic and reorder-resilient):

1) During import, each `.omat.json` records texture bindings by stable asset
   identity/address (e.g., `asset://...` URI or VirtualPath/AssetKey), not by
   transient glTF indices.
1) During cook, build a dependency graph for the pack:
   - Discover all texture assets referenced by all materials in the cook set.
   - Ensure the fallback texture is installed at index `0`.
   - Assign each discovered texture a stable `ResourceIndexT` (>= 1) in a
     deterministic order (e.g., by texture asset key, or by VirtualPath).
1) When writing each material descriptor, look up each referenced texture’s
   `ResourceIndexT` and write it into the corresponding slot.
   - If a binding is absent, write `0`.
   - If *all* bindings are absent, also set `kMaterialFlag_NoTextureSampling`.

### ORM packing

glTF commonly packs occlusion/roughness/metallic into one texture (ORM):

- Occlusion = R
- Roughness = G
- Metallic = B

The PAK descriptor currently has separate fields for
`ambient_occlusion_texture`, `roughness_texture`, and `metallic_texture`.
The simplest correct convention (no format changes) is:

- If glTF provides a single ORM texture, store the same texture-table index
  into all three fields.
- Set `kMaterialFlag_GltfOrmPacked`.
- Shader sampling follows:
  - AO from R
  - roughness from G
  - metalness from B

This preserves Blender parity semantics while keeping the descriptor stable.

### Texture transforms & UV sets

`KHR_texture_transform` provides offset/rotation/scale for UVs per texture binding.

#### Is per-texture UV transform important for a game engine?

Yes, in practice.

Even in real-time pipelines, per-binding UV transforms are a common authoring
tool to:

- Tile a detail map (scale) without duplicating meshes.
- Atlas textures (offset/scale) without rebaking UVs.
- Flip/offset normal/ORM/emissive bindings for variants.

If we claim Tier 1 support and want Blender parity, we must not silently drop
these transforms.

#### Preservation strategy (importer + cook + runtime)

The transform is per *texture binding*, so baking into the mesh is not a
general solution (a single UV set cannot encode multiple different transforms
for different textures).

Required behavior:

- Importer always preserves `KHR_texture_transform` in the authoring material
  (`.omat.json`) per binding.
- Cooker propagates it into cooked material data.
- Runtime/shaders apply it per sampled texture.

#### Strategy: full interop UV transforms (required)

We support **full per-binding UV transforms** with two equivalent
representations:

- **SRT mode** (authoring-friendly): Scale + Rotation + Translation (offset).
- **Matrix mode** (runtime-friendly): an affine UV transform as a 2×3 matrix.

The importer, cooker, and runtime have distinct responsibilities:

1. **Importer / authoring (`.omat.json`)**

- Importer reads glTF `KHR_texture_transform` exactly as authored:
  - `offset` (translation)
  - `scale`
  - `rotation`
  - `texCoord` (UV set selector)
- Importer preserves these per *binding* in `.omat.json`.
- `.omat.json` MAY also carry an equivalent **matrix mode** representation for
  tooling workflows, but glTF import itself is defined in terms of glTF’s SRT
  fields so that the authoring data stays interoperable.

1. **Cooked/binary material format**

- The cooked/binary format stores **only matrix mode**.
- If the authoring data is SRT mode, cook **must** compute the equivalent 2×3
  matrix and emit that.
- This is intentional:
  - Keeps runtime/shaders simple and uniform (one representation).
  - Avoids per-sample trig in shaders.
  - Allows exact rotation support when representable.

Matrix definition (UV affine 2×3):

Let the matrix be:

  [ a b c ]
  [ d e f ]

Applied as:

  uv' = (a*uv.x + b*uv.y + c,
         d*uv.x + e*uv.y + f)

Cook must compute this matrix from SRT in a way compatible with glTF semantics.

1. **Shader codegen/runtime contract**

For each texture binding that can sample a texture, the runtime provides:

- `texCoord` (which UV set to use), and
- `uvTransform` as a 2×3 affine matrix (two rows of 3 scalars).

Shaders must apply the transform *per binding* before sampling:

- Select the requested UV set (`TEXCOORD_n`).
- Apply the binding’s 2×3 transform to that UV.
- Sample the binding’s texture with the transformed UV.

Note: the constant/binary layouts may use float16 or float32 encodings for the
matrix elements depending on desired precision vs size. This design does **not**
cap the format at 256 bytes; if the descriptor grows, any remaining slack should
still be placed in a trailing `reserved` region for future evolution.

#### glTF compatibility (import interop)

Importer compatibility rules for `KHR_texture_transform`:

- SRT fields are preserved in `.omat.json` per binding without baking.
- Rotation is supported end-to-end via the cooked matrix representation.
- `texCoord` is preserved per binding. If the engine currently supports only
  `TEXCOORD_0`, behavior must be explicit:
  - Strict mode: fail import/cook with a diagnostic.
  - Permissive mode: warn and remap to `TEXCOORD_0` (documented mismatch).

### Alpha modes

glTF supports `OPAQUE`, `MASK`, `BLEND`.

Oxygen represents glTF alpha intent using `oxygen::data::MaterialDomain`:

- `OPAQUE`  `MaterialDomain::kOpaque`
- `MASK`  `MaterialDomain::kMasked` (uses `alphaCutoff`)
- `BLEND`  `MaterialDomain::kAlphaBlended`

Importer/cooker requirements:

- Preserve `alphaMode` and `alphaCutoff` in authoring material (`.omat.json`).
- Cook maps `alphaMode` to `MaterialDomain` deterministically.
- Runtime/render passes route by domain (opaque vs masked vs alpha-blended).

Notes:

- `doubleSided` is independent of `MaterialDomain` (raster state / flag).
- `alphaCutoff` is meaningful only for `MASK`.

## Scene extraction

### Authoring scene (`.oscene.json`)

**Decision (2026-01-01):** The generated `.oscene.json` from glTF import must be a **normal, editable scene document**.

This means: do not invent a separate “imported scene schema”. Instead, glTF scene import must emit JSON that conforms to the editor’s existing scene serialization model in `Oxygen.Editor.World.Serialization` (i.e., `SceneData` / `SceneNodeData` + polymorphic `ComponentData` and `OverrideSlotData`).

The current glTF importer output (`SceneSource`/`SceneNodeSource`) is **not sufficient** for the editor and must be rewritten.

#### Required contract (what must work when opening in the editor)

- The editor loads the scene and shows a correct, navigable hierarchy.
- Meshes are visible by default (geometry components exist and reference the imported geometry assets).
- TRS transforms match the source glTF.
- Nodes have stable non-empty names.
- The scene is editable (rename/reparent/add components, etc.) and can be saved using the normal editor serializer.

#### Concrete mapping to the existing schema

The importer must generate a `.oscene.json` that is effectively a `SceneData` document:

- `SceneData.Id`: deterministic GUID for this imported scene document.
- `SceneData.Name`: glTF scene name or derived filename.
- `SceneData.RootNodes`: one `SceneNodeData` per glTF node in the default scene’s root set.

Each `SceneNodeData` must include:

- `Id`: deterministic GUID for the node.
- `Name`: `node.name` or a synthesized name if missing/duplicate.
- `Components`:
  - Always include a `TransformData` entry (the editor expects a transform component).
    - Map glTF local transform to `TransformData.LocalPosition`, `TransformData.LocalRotation`, `TransformData.LocalScale`.
  - If glTF node has a mesh: include a `GeometryComponentData` entry.
    - `GeometryComponentData.GeometryUri` must reference the imported `.ogeo` as an `asset://` URI.
    - If per-submesh materials must be enforced at the scene level (instead of being baked into the `.ogeo` metadata), use `GeometryComponentData.TargetedOverrides` with `MaterialsSlotData`.
- `Children`: recursively emit child `SceneNodeData` for glTF visual children.

Notes:

- Mesh instancing is naturally supported: multiple nodes can reference the same `GeometryUri`.
- Cameras are supported by the existing schema (`PerspectiveCameraData` / `OrthographicCameraData` are already registered). Import should emit those components when glTF cameras are present and mapped.
- Lights are not currently present in the editor schema registration list; importer can ignore or defer lights until a light component exists in the schema.

#### Deterministic GUID strategy (needed for editability + reimport)

Because the editor schema requires GUIDs (`SceneData.Id`, `SceneNodeData.Id`), the importer must generate them deterministically so that:

- Re-import can update/merge without exploding the scene into “all-new” nodes.
- User edits can be preserved by stable node identity.

Proposed approach:

- Derive GUIDs from stable strings (e.g., source asset key + node traversal path) using a stable hash-to-GUID function.
- Store the exact derivation/version in the sidecar import metadata to keep it forward-compatible.

#### Concrete enhancements to the existing schema (minimal, additive)

To make scenes *more powerful* without introducing a new schema, add these optional fields (backward compatible) to the existing DTOs:

- `SceneData.ImportMetadata` (optional): source glTF reference, importer version, and settings used.
- `SceneNodeData.Extras` (optional): JSON object blob to preserve glTF `extras` without affecting runtime behavior.

These additions improve provenance and reimport fidelity while staying within the single existing scene document model.

## Skins (skeletons) and animations

**Status (2026-01-01): deferred (future).** Skins and animations are not part of the current Tier 0–2 milestone.

glTF skinning model:

- `skins[]` define `joints[]` and optional `inverseBindMatrices` accessor.
- Nodes reference a skin via `node.skin`.
- Mesh primitives must provide `JOINTS_0` and `WEIGHTS_0` (and optionally additional sets).

### Import strategy

Two viable Oxygen strategies:

1) Treat skins and animations as part of the Scene asset (scene owns skeleton + animation clips).
2) Introduce dedicated `Skeleton` / `AnimationClip` assets and reference them from Scene.

Deferred decision (future): which aligns with Oxygen’s engine architecture and asset system.

### Minimum correctness requirements

- Support `JOINTS_0` and `WEIGHTS_0` (4 influences).
- Correctly decode quantized joint weights (normalized ints) and renormalize.
- Correctly apply inverse bind matrices.

### Animation decoding

glTF animations target only:

- Node translation/rotation/scale
- Node morph weights

Interpolation:

- `LINEAR` (rotations should use slerp)
- `STEP`
- `CUBICSPLINE` (tripled output stream: in-tangent, value, out-tangent)

Importer should preserve raw keyframes in authoring assets and/or sample to a fixed rate at cook time.

Deferred decision (future): do we store curves (time/value) or bake to a fixed FPS? (Godot exposes an explicit FPS bake knob; Blender has multiple bake/merge modes.)

## Morph targets (blend shapes)

**Status (2026-01-01): deferred (future).** Morph targets are not part of the current Tier 0–2 milestone.

glTF morph targets are per-primitive deltas added to base attributes, driven by weights.

Support requires:

- Storing per-target deltas for POSITION/NORMAL/TANGENT (at least).
- Handling sparse accessors efficiently.

Deferred decisions (future): morph target runtime and asset representation.

Current importer behavior (until implemented):

- Strict mode: fail import when morph targets are present.
- Permissive mode: warn and import the base mesh only (ignore morph targets and weights).

## Lights and cameras

### `KHR_lights_punctual`

This extension adds directional/point/spot lights.

Import should map:

- Light type
- Color
- Intensity
- Range
- Spot cone angles

Open decision: does Oxygen use physical light units compatible with glTF’s intent, or a unitless model?

### Cameras

glTF supports perspective and orthographic cameras.

Import should carry the camera definition and attach it to the node in scene authoring/cooked output.

Open decision: which camera parameters are supported by Oxygen’s camera component.

## Extras and custom properties

glTF supports `extras` on most objects.

Recommended policy:

- Preserve `extras` optionally in authoring scene/material assets for tooling.
- Do not let `extras` affect runtime unless explicitly whitelisted.

### Decision (2026-01-01): Option A

- Preserve `extras` as **opaque JSON** for tooling/reimport traceability (e.g., `SceneNodeData.Extras`, and optional material authoring fields).
- Do **not** treat any `extras` keys as importer “hints”.
- Runtime behavior is unaffected by `extras`.

If we later want “import hints”, they must be an explicit, opt-in feature with a documented schema (not implicit interpretation of arbitrary `extras`).

## Diagnostics & strictness

Importer should support at least two modes:

- **Strict**: fail on unsupported `extensionsRequired`, invalid accessors, unsupported primitive modes, unsafe URIs, etc.
- **Permissive**: warn and best-effort load, but never silently produce incorrect cooked outputs.

Diagnostics must:

- Include a stable “path” to the offending element (e.g., `meshes[3].primitives[1].attributes.NORMAL`).
- Differentiate fatal errors vs warnings.

## Verification strategy

Suggested importer validation corpus:

- Minimal hand-authored glTFs that exercise:
  - Interleaved `byteStride`
  - Normalized integer attributes (UVs/colors/weights)
  - Sparse accessors
  - Missing normals/tangents
  - Multiple primitives/material slots
  - Negative scale transforms
  - `KHR_texture_transform`

Open decision: whether to vendor a subset of Khronos sample models into tests (license/size considerations) or generate minimal assets in-test.

## Open decisions / questions for you

Please confirm or choose:

1) **Embedded image URIs (`data:`) support**

- Allow (recommended for full glTF compliance)
- Disallow (simplifies safety model but breaks embedded `.gltf` workflows)

1) **Where to store color space** (sRGB vs linear)

- On `.otex` (texture asset)
- On material texture binding
- In shader permutation only

1) **Sampler state ownership**

- Per texture
- Per material binding
- Fixed engine samplers only

1) **Imported scene authoring format**

- Use the existing editor scene schema (`SceneData` / `SceneNodeData` + components/slots) for `.oscene.json` (decision: yes)
- **Decision:** add optional `ImportMetadata` and `Extras` fields to improve reimport and traceability.

1) **Geometry asset granularity**

- One `.ogeo` per glTF mesh with multiple submeshes (recommended)
- One `.ogeo` per primitive

1) **Feature tier for next milestone**

- **Decision (2026-01-01): Tier 0 + Tier 1 + Tier 2** (Blender-parity target for typical game assets).
- Tier 3 mesh compression (`EXT_meshopt_compression`, `KHR_draco_mesh_compression`) remains a separate compatibility goal and can be scheduled independently.

1) **Skins/animations/morph targets**

- Deferred (future): not part of the current Tier 0–2 milestone.

1) **Blender parity validation baseline**

- Pin the Blender version and the exact viewport/render color-management settings used as the reference for “we see what Blender sees” validation (so diffs are stable and actionable).
