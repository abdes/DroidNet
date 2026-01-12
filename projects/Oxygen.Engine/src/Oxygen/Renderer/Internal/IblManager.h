//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine::internal {

class IblManager {
public:
  OXYGEN_MAKE_NON_COPYABLE(IblManager);
  OXYGEN_MAKE_NON_MOVABLE(IblManager);

  struct Config {
    uint32_t irradiance_size { 32 };
    uint32_t prefilter_size { 256 }; // 128 or 256 common for split-sum
  };

  explicit IblManager(observer_ptr<Graphics> gfx, Config config = {});
  ~IblManager();

  //! Ensures resources (textures, views) are created.
  //! @return True if successful.
  auto EnsureResourcesCreated() -> bool;

  //! Returns the Shader Resource View index for the diffuse irradiance map.
  [[nodiscard]] auto GetIrradianceMapSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns the Shader Resource View index for the specular prefilter map.
  [[nodiscard]] auto GetPrefilterMapSlot() const noexcept -> ShaderVisibleIndex;

  //! Returns the UAV index for a specific mip of the prefilter map.
  [[nodiscard]] auto GetPrefilterMapUavSlot(uint32_t mip_level) const noexcept
    -> ShaderVisibleIndex;

  //! Returns the UAV index for the irradiance map.
  [[nodiscard]] auto GetIrradianceMapUavSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns the Irradiance map texture.
  [[nodiscard]] auto GetIrradianceMap() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the Prefilter map texture.
  [[nodiscard]] auto GetPrefilterMap() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the source cubemap slot that was used for the last generation.
  [[nodiscard]] auto GetLastSourceCubemapSlot() const noexcept
    -> ShaderVisibleIndex
  {
    return last_source_cubemap_slot_;
  }

  //! Marks that generation has completed for the given source slot.
  auto MarkGenerated(ShaderVisibleIndex source_slot) -> void;

  //! Checks if the given source slot is different from the last cached one.
  [[nodiscard]] auto IsDirty(ShaderVisibleIndex source_slot) const noexcept
    -> bool;

  [[nodiscard]] auto GetConfig() const noexcept -> const Config&
  {
    return config_;
  }

private:
  struct MapResources {
    std::shared_ptr<graphics::Texture> texture;
    graphics::NativeView srv_view;
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };

    // For prefilter, we need UAVs for each mip level.
    // For irradiance, just one UAV.
    std::vector<graphics::NativeView> uav_views;
    std::vector<ShaderVisibleIndex> uav_indices;
  };

  auto CleanupResources() -> void;
  auto CreateMapTexture(uint32_t size, uint32_t mip_levels, const char* name)
    -> std::shared_ptr<graphics::Texture>;
  auto CreateViews(MapResources& map) -> bool;

  observer_ptr<Graphics> gfx_;
  Config config_;
  bool resources_created_ { false };

  ShaderVisibleIndex last_source_cubemap_slot_ { kInvalidShaderVisibleIndex };

  MapResources irradiance_map_;
  MapResources prefilter_map_;
};

} // namespace oxygen::engine::internal
