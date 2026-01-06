//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Material asset as stored in the PAK file resource table.
/*!
 Represents a material asset as described by the PAK file's MaterialAssetDesc.
 This is a direct, binary-compatible wrapper for the PAK format, providing
 access to all fields and metadata for rendering and asset management.

 ### Binary Encoding (PAK v1, 256 bytes)

 ```text
 offset size   name                  description
 ------ ------ --------------------- -------------------------------------------
 0x00   96     header                AssetHeader (type, name, version, etc.)
 0x5F   1      material_domain       Material domain (enum)
 0x60   4      flags                 Bitfield for material options
 0x64   4      shader_stages         Bitfield for used shader stages
 0x68   16     base_color            RGBA fallback color (float[4])
 0x78   4      normal_scale          Normal map scale (float)
 0x7C   2      metalness             Metalness scalar (UNorm16)
 0x7E   2      roughness             Roughness scalar (UNorm16)
 0x80   2      ambient_occlusion     AO scalar (UNorm16)
 0x82   4      base_color_texture    Index into TextureResourceTable
 0x86   4      normal_texture        Index into TextureResourceTable
 0x8A   4      metallic_texture      Index into TextureResourceTable
 0x8E   4      roughness_texture     Index into TextureResourceTable
 0x92   4      ambient_occlusion_tex Index into TextureResourceTable
 0x96   28     tier2_textures        Tier 1/2 texture indices (7 slots)
 0xB2   46     tier2_params          Tier 1/2 scalar params (UNorm16 + F16)
 0xD8   40     reserved              Reserved for future expansion
 0x100 ...     shader references     Array ShaderReference
 ```

 @note The shader indices array immediately follows the descriptor and is sized
 by the number of set bits in `shader_stages`.

 @see MaterialAssetDesc, TextureResourceDesc, ShaderReferenceDesc, PakFormat.h
*/
class MaterialAsset : public Asset {
  OXYGEN_TYPED(MaterialAsset)

public:
  explicit MaterialAsset(AssetKey asset_key, pak::MaterialAssetDesc desc,
    std::vector<ShaderReference> shader_refs = {},
    std::vector<oxygen::content::ResourceKey> texture_resource_keys = {})
    : Asset(asset_key)
    , desc_(std::move(desc))
    , shader_refs_(std::move(shader_refs))
    , texture_resource_keys_(std::move(texture_resource_keys))
  {
  }

  ~MaterialAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MaterialAsset)
  OXYGEN_DEFAULT_MOVABLE(MaterialAsset)

  //! Returns the asset header metadata.
  [[nodiscard]] auto GetHeader() const noexcept -> const pak::AssetHeader&
  {
    return desc_.header;
  }

  //! Returns the material domain (e.g. Opaque, AlphaBlended, etc.).
  [[nodiscard]] auto GetMaterialDomain() const noexcept -> MaterialDomain
  {
    return static_cast<MaterialDomain>(desc_.material_domain);
  }

  //! Returns the material flags bitfield.
  [[nodiscard]] auto GetFlags() const noexcept -> uint32_t
  {
    return desc_.flags;
  }

  //! Returns whether this material is double-sided.
  /*!
   Double-sided is an explicit material property (encoded in the PAK material
   flags). Render passes use it to disable backface culling.

   @return True if the material should be treated as double-sided.
  */
  [[nodiscard]] auto IsDoubleSided() const noexcept -> bool
  {
    return (GetFlags() & pak::kMaterialFlag_DoubleSided) != 0u;
  }

  //! Returns the shader references for all stages used by this material.
  [[nodiscard]] auto GetShaders() const noexcept
    -> std::span<const ShaderReference>
  {
    return std::span<const ShaderReference>(
      shader_refs_.data(), shader_refs_.size());
  }

  //! Returns the fallback base color (RGBA).
  [[nodiscard]] auto GetBaseColor() const noexcept -> std::span<const float, 4>
  {
    return std::span<const float, 4>(desc_.base_color);
  }

  //! Returns the normal map scale.
  [[nodiscard]] auto GetNormalScale() const noexcept -> float
  {
    return desc_.normal_scale;
  }

  //! Returns the metalness scalar.
  [[nodiscard]] auto GetMetalness() const noexcept -> float
  {
    return desc_.metalness.ToFloat();
  }

  //! Returns the roughness scalar.
  [[nodiscard]] auto GetRoughness() const noexcept -> float
  {
    return desc_.roughness.ToFloat();
  }

  //! Returns the ambient occlusion scalar.
  [[nodiscard]] auto GetAmbientOcclusion() const noexcept -> float
  {
    return desc_.ambient_occlusion.ToFloat();
  }

  //! Returns the alpha cutoff threshold for alpha-tested materials.
  [[nodiscard]] auto GetAlphaCutoff() const noexcept -> float
  {
    return desc_.alpha_cutoff.ToFloat();
  }

  //! Returns the index of the base color texture.
  [[nodiscard]] auto GetBaseColorTexture() const noexcept -> pak::ResourceIndexT
  {
    return desc_.base_color_texture;
  }

  //! Returns the index of the normal texture.
  [[nodiscard]] auto GetNormalTexture() const noexcept -> pak::ResourceIndexT
  {
    return desc_.normal_texture;
  }

  //! Returns the index of the metallic texture.
  [[nodiscard]] auto GetMetallicTexture() const noexcept -> pak::ResourceIndexT
  {
    return desc_.metallic_texture;
  }

  //! Returns the index of the roughness texture.
  [[nodiscard]] auto GetRoughnessTexture() const noexcept -> pak::ResourceIndexT
  {
    return desc_.roughness_texture;
  }

  //! Returns the index of the ambient occlusion texture.
  [[nodiscard]] auto GetAmbientOcclusionTexture() const noexcept
    -> pak::ResourceIndexT
  {
    return desc_.ambient_occlusion_texture;
  }

  //! Creates a default material for procedural meshes and fallback scenarios.
  OXGN_DATA_NDAPI static auto CreateDefault()
    -> std::shared_ptr<const MaterialAsset>;

  //! Creates a debug/wireframe material for development and debugging.
  OXGN_DATA_NDAPI static auto CreateDebug()
    -> std::shared_ptr<const MaterialAsset>;

  //! Set runtime-only per-slot texture resource keys.
  /*!
   This is used by async publish code to fill the source-aware `ResourceKey`
   values after worker-thread decode.

   @param texture_resource_keys Per-slot texture keys in the order:
     base_color, normal, metallic, roughness, ambient_occlusion.
  */
  auto SetTextureResourceKeys(
    std::vector<oxygen::content::ResourceKey> texture_resource_keys) -> void
  {
    texture_resource_keys_ = std::move(texture_resource_keys);
  }

private:
  pak::MaterialAssetDesc desc_ {};
  std::vector<ShaderReference> shader_refs_ {};
  // Runtime-only: per-slot source-aware resource keys produced by loader.
  // Order matches getters: base_color, normal, metallic, roughness,
  // ambient_occlusion
  std::vector<oxygen::content::ResourceKey> texture_resource_keys_ {};

public:
  //! Runtime accessor for source-aware ResourceKey for base color texture.
  [[nodiscard]] auto GetBaseColorTextureKey() const noexcept
    -> oxygen::content::ResourceKey
  {
    if (texture_resource_keys_.size() > 0) {
      return texture_resource_keys_[0];
    }
    return oxygen::content::ResourceKey { 0 };
  }

  [[nodiscard]] auto GetNormalTextureKey() const noexcept
    -> oxygen::content::ResourceKey
  {
    if (texture_resource_keys_.size() > 1) {
      return texture_resource_keys_[1];
    }
    return oxygen::content::ResourceKey { 0 };
  }

  [[nodiscard]] auto GetMetallicTextureKey() const noexcept
    -> oxygen::content::ResourceKey
  {
    if (texture_resource_keys_.size() > 2) {
      return texture_resource_keys_[2];
    }
    return oxygen::content::ResourceKey { 0 };
  }

  [[nodiscard]] auto GetRoughnessTextureKey() const noexcept
    -> oxygen::content::ResourceKey
  {
    if (texture_resource_keys_.size() > 3) {
      return texture_resource_keys_[3];
    }
    return oxygen::content::ResourceKey { 0 };
  }

  [[nodiscard]] auto GetAmbientOcclusionTextureKey() const noexcept
    -> oxygen::content::ResourceKey
  {
    if (texture_resource_keys_.size() > 4) {
      return texture_resource_keys_[4];
    }
    return oxygen::content::ResourceKey { 0 };
  }
};

} // namespace oxygen::data
