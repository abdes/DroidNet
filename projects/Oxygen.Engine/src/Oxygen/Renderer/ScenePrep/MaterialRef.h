//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <cstdint>
#include <span>
#include <array>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace oxygen::engine::sceneprep {

//! Lightweight renderer-facing reference to a material with source-aware
//! texture keys for binding and loading.
struct MaterialRef {
  std::shared_ptr<const oxygen::data::MaterialAsset> asset;

  [[nodiscard]] auto GetFlags() const -> uint32_t
  {
    return asset ? asset->GetFlags() : 0U;
  }

  [[nodiscard]] auto GetMaterialDomain() const -> oxygen::data::MaterialDomain
  {
    return asset ? asset->GetMaterialDomain() : oxygen::data::MaterialDomain::kOpaque;
  }

  [[nodiscard]] auto GetBaseColor() const -> std::span<const float, 4>
  {
    if (asset) {
      return asset->GetBaseColor();
    }
    static const std::array<float, 4> kDefaultBaseColor { 0.0F, 0.0F, 0.0F, 1.0F };
    return std::span<const float, 4>(kDefaultBaseColor);
  }

  [[nodiscard]] auto GetNormalScale() const -> float
  {
    return asset ? asset->GetNormalScale() : 0.0f;
  }

  [[nodiscard]] auto GetMetalness() const -> float
  {
    return asset ? asset->GetMetalness() : 0.0f;
  }

  [[nodiscard]] auto GetRoughness() const -> float
  {
    return asset ? asset->GetRoughness() : 0.0f;
  }

  [[nodiscard]] auto GetAmbientOcclusion() const -> float
  {
    return asset ? asset->GetAmbientOcclusion() : 0.0f;
  }

  [[nodiscard]] auto GetBaseColorTextureKey() const
    -> oxygen::content::ResourceKey
  {
    return asset ? asset->GetBaseColorTextureKey() : 0;
  }

  [[nodiscard]] auto GetNormalTextureKey() const
    -> oxygen::content::ResourceKey
  {
    return asset ? asset->GetNormalTextureKey() : 0;
  }

  [[nodiscard]] auto GetMetallicTextureKey() const
    -> oxygen::content::ResourceKey
  {
    return asset ? asset->GetMetallicTextureKey() : 0;
  }

  [[nodiscard]] auto GetRoughnessTextureKey() const
    -> oxygen::content::ResourceKey
  {
    return asset ? asset->GetRoughnessTextureKey() : 0;
  }

  [[nodiscard]] auto GetAmbientOcclusionTextureKey() const
    -> oxygen::content::ResourceKey
  {
    return asset ? asset->GetAmbientOcclusionTextureKey() : 0;
  }
};

} // namespace oxygen::engine::sceneprep
