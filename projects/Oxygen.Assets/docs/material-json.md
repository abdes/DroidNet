# Material Source JSON (Authoring)

This document specifies the **authoring-time** JSON format for materials.

- Source material JSON lives in the project `Content/**` tree and is consumed by the editor import/build pipeline.
- Runtime never reads this JSON. Runtime consumes the cooked binary `.omat` descriptor defined by `pak::MaterialAssetDesc`.

Design goals:

- Start from the glTF 2.0 **PBR metallic-roughness** model.
- Keep the schema small and directly mappable to the runtime descriptor (`MaterialAssetDesc`).
- Avoid ambiguous path formats: all references are stable and normalized.

## File naming and location

Recommended source extension:

- `*.omat.json`

These files may be hand-authored or produced/updated by an importer (e.g., importing a glTF with materials).

Example:

- `Content/Materials/Wood.omat.json`

Cooked output (derived):

- `/Content/Materials/Wood.omat` (virtual path)
- `.cooked/Content/Materials/Wood.omat` (project-relative cooked file)

## Top-level schema

Top-level is a single JSON object:

```json
{

  "Schema": "oxygen.material.v1",
  "Type": "PBR",
  "Name": "Wood",
  "PbrMetallicRoughness": {
    "BaseColorFactor": [1, 1, 1, 1],
    "MetallicFactor": 0.0,
    "RoughnessFactor": 0.8,
    "BaseColorTexture": { "Source": "asset:///Content/Textures/Wood_BaseColor.png" },
    "MetallicRoughnessTexture": { "Source": "asset:///Content/Textures/Wood_MR.png" }
  },
  "NormalTexture": { "Source": "asset:///Content/Textures/Wood_Normal.png", "Scale": 1.0 },
  "OcclusionTexture": { "Source": "asset:///Content/Textures/Wood_AO.png", "Strength": 1.0 },

  "AlphaMode": "OPAQUE",
  "AlphaCutoff": 0.5,
  "DoubleSided": false
}
```

### Required fields

- `Schema` (string): must be `"oxygen.material.v1"`.
- `Type` (string enum): must be `"PBR"` (MVP).

Everything else is optional (defaults apply).

### Optional fields

- `Type` is required (see above). Future expansion is expected (e.g. `"Unlit"`, `"Clearcoat"`, `"Sheen"`, `"Subsurface"`, `"Layered"`, `"ShaderGraph"`).
- `Name` (string): debugging/display name. If absent, tooling may derive a name from the source filename.

#### `PbrMetallicRoughness` (object)

Matches glTF’s `pbrMetallicRoughness` with a constrained subset.

- `BaseColorFactor` (number[4]): RGBA, default `[1,1,1,1]`.
- `MetallicFactor` (number): default `1.0`.
- `RoughnessFactor` (number): default `1.0`.
- `BaseColorTexture` (TextureRef): optional.
- `MetallicRoughnessTexture` (TextureRef): optional.

#### `NormalTexture` (TextureRefWithScale)

- `Source` (string): optional.
- `Scale` (number): default `1.0`.

#### `OcclusionTexture` (TextureRefWithStrength)

- `Source` (string): optional.
- `Strength` (number): default `1.0`.

#### `AlphaMode` / `AlphaCutoff` / `DoubleSided`

Matches glTF semantics:

- `AlphaMode` (string enum): `"OPAQUE" | "MASK" | "BLEND"`.
  - Default: `"OPAQUE"`.
- `AlphaCutoff` (number): only meaningful for `"MASK"`. Default `0.5`.
- `DoubleSided` (bool): default `false`.

## TextureRef objects

Texture references are **source references** (authoring inputs), not cooked indices.

### TextureRef

```json
{ "Source": "asset:///Content/Textures/Wood.png" }
```

Rules:

- `Source` is an `asset:///{MountPoint}/{Path}` URI.
- The URI must use `/` separators.
- The URI should point to a **source** texture (e.g. `.png`) under `Content/**`.

Notes:

- glTF also carries `texCoord` and texture transform extensions; those are not in MVP.
- If present in input JSON anyway, unknown fields should be ignored (forward compatibility), but tooling may warn.

### TextureRefWithScale

```json
{ "Source": "asset:///Content/Textures/Wood_Normal.png", "Scale": 1.0 }
```

### TextureRefWithStrength

```json
{ "Source": "asset:///Content/Textures/Wood_AO.png", "Strength": 1.0 }
```

## Validation rules (MVP)

- Numbers must be finite.
- `BaseColorFactor` must have exactly 4 components.
- Scalar ranges:
  - `MetallicFactor` should be clamped to `[0,1]`.
  - `RoughnessFactor` should be clamped to `[0,1]`.
  - `Scale` should be clamped to `>= 0` (typical range `[0,1]`).
  - `Strength` should be clamped to `[0,1]`.
  - `AlphaCutoff` should be clamped to `[0,1]`.

## Mapping to cooked `.omat` (runtime descriptor)

The cooked output is a binary `.omat` file whose first 256 bytes are `oxygen::data::pak::MaterialAssetDesc`.

Relevant runtime fields (from `PakFormat.h`):

- `header.asset_type` → `AssetType::kMaterial`.
- `header.name` → `Name` (or derived).
- `header.version` → current `.omat` descriptor version (initially `1`).
- `material_domain` → derived from `AlphaMode` (see below).
- `flags` → derived from `DoubleSided` and alpha settings (see below).
- `shader_stages` → `0` for MVP unless the project introduces shader binding.

Material type mapping:

- If `Type == "PBR"`: use the PBR scalar + texture mapping described in this document.
- Other types are not supported in MVP and should be rejected (hard error) by tooling.

PBR scalar mapping:

- `base_color` ← `PbrMetallicRoughness.BaseColorFactor`
- `metalness` ← `PbrMetallicRoughness.MetallicFactor`
- `roughness` ← `PbrMetallicRoughness.RoughnessFactor`
- `normal_scale` ← `NormalTexture.Scale` (default `1.0`)
- `ambient_occlusion` ← `OcclusionTexture.Strength` (default `1.0`)

Texture slot mapping:

- `base_color_texture` ← resolved cooked texture resource index for `PbrMetallicRoughness.BaseColorTexture.Source`
- `normal_texture` ← resolved cooked texture resource index for `NormalTexture.Source`
- `metallic_texture` and `roughness_texture`:
  - glTF typically packs metallic (B) and roughness (G) into a single texture.
  - MVP policy:
    - Treat `MetallicRoughnessTexture.Source` as a dependency, but emit `0` for both indices.
    - When texture packing support is implemented, define a deterministic unpacking policy and emit the correct indices.
- `ambient_occlusion_texture` ← resolved cooked AO texture resource index for `OcclusionTexture.Source` (or `0`).

Alpha / domain mapping:

- If `AlphaMode == "OPAQUE"`:
  - `material_domain = Opaque`
- If `AlphaMode == "MASK"`:
  - `material_domain = Masked`
  - `AlphaCutoff` should influence an engine-defined flag/parameter policy (not stored in `MaterialAssetDesc` yet).
- If `AlphaMode == "BLEND"`:
  - `material_domain = AlphaBlended`

Flags:

- `DoubleSided` should set the engine’s “double-sided” material flag bit.
- Alpha test / blending flags are engine-defined; the JSON fields provide the intent.

Important: this JSON spec does **not** define numeric bit positions for `flags`. Those must be defined in a shared engine/tooling header to keep the meaning stable across C++ and C#.

## Example: scalar-only MVP material

```json
{
  "Schema": "oxygen.material.v1",
  "Type": "PBR",
  "Name": "DebugMagenta",
  "PbrMetallicRoughness": {
    "BaseColorFactor": [1, 0, 1, 1],
    "MetallicFactor": 0.0,
    "RoughnessFactor": 1.0
  },
  "AlphaMode": "OPAQUE",
  "DoubleSided": false
}
```
