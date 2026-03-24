//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

namespace oxygen::renderer::vsm::testing {

class VirtualShadowTest : public ::testing::Test { };

class VsmPhysicalPoolTestBase : public VirtualShadowTest {
protected:
  [[nodiscard]] static auto MakeShadowPoolConfig(
    const char* debug_name = "vsm-shadow-pool") -> VsmPhysicalPoolConfig
  {
    return VsmPhysicalPoolConfig {
      .page_size_texels = 128,
      .physical_tile_capacity = 512,
      .array_slice_count = 2,
      .depth_format = Format::kDepth32,
      .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
        VsmPhysicalPoolSliceRole::kStaticDepth },
      .debug_name = debug_name,
    };
  }

  [[nodiscard]] static auto MakeHzbPoolConfig(
    const char* debug_name = "vsm-hzb-pool") -> VsmHzbPoolConfig
  {
    return VsmHzbPoolConfig {
      .mip_count = 10,
      .format = Format::kR32Float,
      .debug_name = debug_name,
    };
  }
};

class VsmCacheManagerTestBase : public VsmPhysicalPoolTestBase {
protected:
  [[nodiscard]] static auto MakeSinglePageLocalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const char* frame_name = "vsm-frame",
    const std::uint32_t local_light_count = 1U) -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);
    for (std::uint32_t i = 0; i < local_light_count; ++i) {
      const auto suffix = std::to_string(i);
      address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
        .remap_key = "local-" + suffix,
        .debug_name = "local-" + suffix,
      });
    }

    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeSeam(VsmPhysicalPagePoolManager& pool_manager,
    const VsmVirtualAddressSpaceFrame& current_frame,
    const VsmVirtualAddressSpaceFrame* previous_frame = nullptr)
    -> VsmCacheManagerSeam
  {
    return VsmCacheManagerSeam {
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
      .current_frame = current_frame,
      .previous_to_current_remap = previous_frame != nullptr
        ? BuildVirtualRemapTable(*previous_frame, current_frame)
        : VsmVirtualRemapTable {},
    };
  }

  [[nodiscard]] static auto MakeSeam(VsmPhysicalPagePoolManager& pool_manager,
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const char* frame_name = "vsm-frame",
    const std::uint32_t local_light_count = 1U) -> VsmCacheManagerSeam
  {
    return MakeSeam(pool_manager,
      MakeSinglePageLocalFrame(
        frame_generation, first_virtual_id, frame_name, local_light_count));
  }

  static auto ExtractReadyFrame(VsmCacheManager& manager,
    const VsmCacheManagerSeam& seam, const char* debug_name = "vsm-ready")
    -> void
  {
    manager.BeginFrame(
      seam, VsmCacheManagerFrameConfig { .debug_name = debug_name });
    static_cast<void>(manager.BuildPageAllocationPlan());
    static_cast<void>(manager.CommitPageAllocationFrame());
    manager.ExtractFrameData();
  }
};

} // namespace oxygen::renderer::vsm::testing
