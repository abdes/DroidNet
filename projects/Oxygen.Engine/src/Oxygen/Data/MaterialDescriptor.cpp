//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Core/Types/ShaderRequestRules.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/MaterialDescriptor.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Vertex.h>

namespace {

namespace render = oxygen::data::pak::render;
using oxygen::ShaderType;
using oxygen::data::CookedDefect;
using oxygen::data::CookedFormatError;
using oxygen::data::MaterialDomain;

// Every flag PakFormat_render.h defines.
constexpr auto kKnownFlags = render::kMaterialFlag_NoTextureSampling
  | render::kMaterialFlag_DoubleSided | render::kMaterialFlag_AlphaTest
  | render::kMaterialFlag_Unlit | render::kMaterialFlag_GltfOrmPacked
  | render::kMaterialFlag_ProceduralGrid
  | render::kMaterialFlag_AmbientOcclusionStrength
  | render::kMaterialFlag_GlossinessTexture;

//! The bits of `stages` that name no pipeline stage; a stage bit is a shader
//! type's value.
auto UnknownStages(uint32_t stages) -> uint32_t
{
  auto unknown = uint32_t { 0 };
  for (; stages != 0U; stages &= stages - 1U) {
    const auto bit = static_cast<uint32_t>(std::countr_zero(stages));
    if (!oxygen::IsShaderStage(bit)) {
      unknown |= 1U << bit;
    }
  }
  return unknown;
}

//! Refuses a slot naming a UV set vertices do not hold, a sampler state outside
//! the table, or a transform that is not finite.
auto CheckTextureSampling(const render::TextureSlotSampling& sampling) -> void
{
  if (sampling.uv_set >= oxygen::data::kUvSetCount) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      fmt::format("material texture UV set {} is unknown", sampling.uv_set));
  }
  const auto known_sampler = sampling.address_u < oxygen::kTextureAddressCount
    && sampling.address_v < oxygen::kTextureAddressCount
    && sampling.mag_filter < oxygen::kTextureFilterCount
    && sampling.min_filter < oxygen::kTextureFilterCount
    && sampling.mip_filter < oxygen::kTextureMipFilterCount;
  if (!known_sampler) {
    throw CookedFormatError(
      CookedDefect::kInvalidValue, "material texture sampler is unknown");
  }
  const auto transform = std::array {
    float { sampling.uv_scale[0] },
    float { sampling.uv_scale[1] },
    float { sampling.uv_offset[0] },
    float { sampling.uv_offset[1] },
    float { sampling.uv_rotation_radians },
  };
  if (!std::ranges::all_of(transform,
        [](const float value) -> bool { return std::isfinite(value); })) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      "material texture UV transform is not finite");
  }
}

auto CheckValues(const render::MaterialAssetDesc& desc) -> void
{
  const auto known_domain
    = desc.material_domain > static_cast<uint8_t>(MaterialDomain::kUnknown)
    && desc.material_domain
      <= static_cast<uint8_t>(MaterialDomain::kMaxMaterialDomain);
  if (!known_domain) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      fmt::format("material domain {} is unknown", desc.material_domain));
  }
  if ((desc.flags & ~kKnownFlags) != 0U) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      fmt::format(
        "material flags 0x{:08X} are unknown", desc.flags & ~kKnownFlags));
  }
  if (const auto unknown = UnknownStages(desc.shader_stages); unknown != 0U) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      fmt::format("material shader stages 0x{:08X} are unknown", unknown));
  }
  const auto emissive_in_range
    = std::ranges::all_of(desc.emissive_factor, [](const float value) -> bool {
        return std::isfinite(value) && value >= 0.0F
          && value <= render::kMaxMaterialEmissiveFactor;
      });
  if (!emissive_in_range) {
    throw CookedFormatError(
      CookedDefect::kInvalidValue, "material emissive factor is out of range");
  }
  for (const auto& sampling : desc.texture_sampling) {
    CheckTextureSampling(sampling);
  }
}

} // namespace

auto oxygen::data::CheckMaterialDescriptor(
  const render::MaterialAssetDesc& desc,
  const std::span<const ShaderReference> shaders) -> void
{
  CheckValues(desc);
  if (std::cmp_not_equal(shaders.size(), std::popcount(desc.shader_stages))) {
    throw CookedFormatError(CookedDefect::kInconsistent,
      fmt::format("material declares {} shader stages and has {} shaders",
        std::popcount(desc.shader_stages), shaders.size()));
  }
  auto stages = desc.shader_stages;
  for (const auto& shader : shaders) {
    const auto expected = static_cast<ShaderType>(std::countr_zero(stages));
    if (shader.GetShaderType() != expected) {
      throw CookedFormatError(CookedDefect::kInconsistent,
        "material shaders are not its declared stages in ascending order");
    }
    stages &= stages - 1U;
  }
}
