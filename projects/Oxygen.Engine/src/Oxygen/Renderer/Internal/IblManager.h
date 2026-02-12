//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Internal/IIblProvider.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine::internal {

class IblPassTag {
  friend struct IblPassTagFactory;
  IblPassTag() noexcept = default;
};

struct IblPassTagFactory {
  static auto Get() noexcept -> IblPassTag;
};

class IblManager : public IIblProvider {
public:
  struct Config {
    uint32_t irradiance_size { 32 };
    uint32_t prefilter_size { 256 }; // 128 or 256 common for split-sum
  };

  explicit IblManager(observer_ptr<Graphics> gfx);
  IblManager(observer_ptr<Graphics> gfx, Config config);
  ~IblManager();

  OXYGEN_MAKE_NON_COPYABLE(IblManager)
  OXYGEN_MAKE_NON_MOVABLE(IblManager)

  [[nodiscard]] auto GetConfig() const noexcept -> const Config&
  {
    return config_;
  }

  //! Ensures resources (textures, views) are created.
  //! @return True if successful.
  auto EnsureResourcesCreated() -> bool override;
  auto EnsureResourcesCreatedForView(ViewId view_id) -> bool;

  //! Query current outputs and generation for a source slot.
  [[nodiscard]] auto QueryOutputsFor(
    ViewId view_id, ShaderVisibleIndex source_slot) const noexcept
    -> IIblProvider::OutputMaps override;

  // -- Interface reserved for IblComputePass only -----------------------------

  //! Returns the UAV index for a specific mip of the prefilter map.
  [[nodiscard]] auto GetPrefilterMapUavSlot(
    IblPassTag tag, ViewId view_id, uint32_t mip_level) const noexcept
    -> ShaderVisibleIndex;

  //! Returns the UAV index for the irradiance map.
  [[nodiscard]] auto GetIrradianceMapUavSlot(
    IblPassTag tag, ViewId view_id) const noexcept -> ShaderVisibleIndex;

  //! Returns the Irradiance map texture.
  [[nodiscard]] auto GetIrradianceMap(IblPassTag tag, ViewId view_id) const
    noexcept -> observer_ptr<graphics::Texture>;

  //! Returns the Prefilter map texture.
  [[nodiscard]] auto GetPrefilterMap(IblPassTag tag, ViewId view_id) const
    noexcept
    -> observer_ptr<graphics::Texture>;

  //! Marks that generation has completed for the given source slot and
  //! advances the generation token.
  auto MarkGenerated(IblPassTag tag, ViewId view_id,
    ShaderVisibleIndex source_slot, std::uint64_t source_content_version)
    -> void;

  auto EraseViewState(ViewId view_id) -> void;

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
  auto CleanupViewResources(ViewId view_id) -> void;
  auto EnsureViewResourcesCreated(ViewId view_id) -> bool;
  auto CreateMapTexture(uint32_t size, uint32_t mip_levels, const char* name)
    -> std::shared_ptr<graphics::Texture>;
  auto CreateViews(MapResources& map) -> bool;

  observer_ptr<Graphics> gfx_;
  Config config_;
  struct ViewState {
    bool resources_created { false };
    ShaderVisibleIndex last_source_cubemap_slot { kInvalidShaderVisibleIndex };
    std::uint64_t last_source_content_version { 0ULL };
    std::atomic<std::uint64_t> generation { 1ULL };
    MapResources irradiance_map;
    MapResources prefilter_map;
  };
  std::unordered_map<ViewId, std::unique_ptr<ViewState>> view_states_;
};

} // namespace oxygen::engine::internal
