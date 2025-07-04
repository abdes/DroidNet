//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics {

/**
 * @brief Sampler descriptor defining all sampling parameters
 */
struct SamplerDesc {
  enum class Filter : uint8_t { kPoint, kBilinear, kTrilinear, kAniso };

  enum class AddressMode : uint8_t { kWrap, kMirror, kClamp, kBorder };

  enum class CompareFunc : uint8_t { kDisabled, kLess, kGreater, kEqual };

  Filter filter = Filter::kBilinear;
  AddressMode address_u = AddressMode::kWrap;
  AddressMode address_v = AddressMode::kWrap;
  AddressMode address_w = AddressMode::kWrap;
  float mip_lod_bias = 0.0f;
  uint32_t max_anisotropy = 16;
  CompareFunc compare_func = CompareFunc::kDisabled;
  float border_color[4] = { 0, 0, 0, 0 };
  float min_lod = 0.0f;
  float max_lod = 1000.0f;

  bool operator==(const SamplerDesc&) const = default;
  [[nodiscard]] size_t GetHash() const noexcept;
};

/**
 * @brief Backend-agnostic sampler class for texture sampling operations
 *
 * This class wraps the backend-specific sampler implementation and provides
 * a consistent interface across different graphics APIs.
 */
class Sampler : public Composition,
                public Named,
                public std::enable_shared_from_this<Sampler> {
public:
  explicit Sampler(std::string_view name)
  {
    AddComponent<ObjectMetaData>(name);
  }

  OXYGEN_GFX_API virtual ~Sampler() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Sampler)
  OXYGEN_DEFAULT_MOVABLE(Sampler)

  //! Gets the native resource handle for the texture.
  [[nodiscard]] virtual auto GetNativeResource() const -> NativeObject = 0;

  //! Gets the descriptor for this texture.
  [[nodiscard]] virtual auto GetDescriptor() const -> const SamplerDesc& = 0;

  //! Gets the name of the texture.
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return GetComponent<ObjectMetaData>().GetName();
  }

  //! Sets the name of the texture.
  void SetName(const std::string_view name) noexcept override
  {
    GetComponent<ObjectMetaData>().SetName(name);
  }
};

// Ensure Sampler satisfies ResourceWithViews
static_assert(oxygen::graphics::SupportedResource<oxygen::graphics::Sampler>,
  "Sampler must satisfy ResourceWithViews");

// Common predefined samplers
namespace Samplers {
  // Creates a point/nearest sampler with clamp address mode
  [[nodiscard]] Sampler PointClamp();

  // Creates a bilinear sampler with clamp address mode
  [[nodiscard]] Sampler BilinearClamp();

  // Creates a trilinear sampler with wrap address mode
  [[nodiscard]] Sampler TrilinearWrap();

  // Creates an anisotropic sampler with wrap address mode
  [[nodiscard]] Sampler AnisotropicWrap(uint32_t max_anisotropy = 16);

  // Creates a shadow map comparison sampler
  [[nodiscard]] Sampler ShadowComparison();
}

} // namespace oxygen::graphics
