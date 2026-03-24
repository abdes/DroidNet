//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

namespace oxygen::renderer::vsm::testing {

class VirtualShadowGpuTest
  : public oxygen::graphics::d3d12::testing::OffscreenTestFixture {
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

class VsmCacheManagerGpuTestBase : public VirtualShadowGpuTest {
protected:
  [[nodiscard]] static auto MakeSeam(VsmPhysicalPagePoolManager& pool_manager,
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const char* frame_name = "vsm-frame",
    const std::uint32_t local_light_count = 1U) -> VsmCacheManagerSeam
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

    return VsmCacheManagerSeam {
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
      .current_frame = address_space.DescribeFrame(),
      .previous_to_current_remap = {},
    };
  }
};

} // namespace oxygen::renderer::vsm::testing
