//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

#include <combaseapi.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgiformat.h>
#include <nlohmann/json.hpp>
#include <winerror.h>
#include <winnt.h>
#include <wrl/client.h>

#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Detail/TextureReadback.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>

namespace oxygen::vortex::testing {

NOLINT_TEST_F(LightingGpuAbiTest, NativeShadowAllocationRequirements)
{
  auto* device = Backend().GetCurrentDevice();
  ASSERT_NE(device, nullptr);
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory2(0U, IID_PPV_ARGS(&factory))));
  Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
  ASSERT_TRUE(SUCCEEDED(factory->EnumAdapterByLuid(
    device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))));
  auto description = DXGI_ADAPTER_DESC2 {};
  ASSERT_TRUE(SUCCEEDED(adapter->GetDesc2(&description)));
  ASSERT_EQ(
    description.Flags & static_cast<std::uint32_t>(DXGI_ADAPTER_FLAG_SOFTWARE),
    0U);
  auto name = std::string {};
  string_utils::WideToUtf8(std::span { description.Description }.data(), name);
  auto memory = DXGI_QUERY_VIDEO_MEMORY_INFO {};
  ASSERT_TRUE(SUCCEEDED(adapter->QueryVideoMemoryInfo(
    0U, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)));
  auto driver = LARGE_INTEGER {};
  ASSERT_TRUE(
    SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)));
  auto report = nlohmann::json {
    { "adapter", name },
    { "dedicated_video_memory_bytes", description.DedicatedVideoMemory },
    { "driver_version_raw", driver.QuadPart },
    { "local_budget_bytes", memory.Budget },
    { "process_local_usage_bytes", memory.CurrentUsage },
    { "shadow_resources_allocated", false },
    {
      "scope",
      "allocation requirements only; not rendering parity or resident frame "
      "peak",
    },
    { "requirements", nlohmann::json::array() },
  };
  struct Request {
    const char* name;
    std::uint32_t resolution;
    std::uint32_t layers;
  };
  constexpr auto requests = std::array {
    Request { .name = "medium_directional", .resolution = 2048U, .layers = 8U },
    Request { .name = "medium_point", .resolution = 1024U, .layers = 24U },
    Request {
      .name = "medium_projected_spot",
      .resolution = 1024U,
      .layers = 8U,
    },
    Request { .name = "medium_cube_spot", .resolution = 1024U, .layers = 48U },
    Request {
      .name = "maximum_directional",
      .resolution = 4096U,
      .layers = 8U,
    },
    Request { .name = "maximum_point", .resolution = 2048U, .layers = 24U },
    Request {
      .name = "maximum_projected_spot",
      .resolution = 2048U,
      .layers = 8U,
    },
    Request { .name = "maximum_cube_spot", .resolution = 2048U, .layers = 48U },
    Request {
      .name = "small_alignment_control",
      .resolution = 32U,
      .layers = 6U,
    },
  };
  for (const auto format : { Format::kDepth32Stencil8, Format::kDepth32 }) {
    const auto* const format_name
      = format == Format::kDepth32 ? "D32" : "D32S8";
    for (const auto& request : requests) {
      SCOPED_TRACE(format_name);
      SCOPED_TRACE(request.name);
      const auto descriptor = graphics::d3d12::detail::MakeTextureResourceDesc(
        graphics::TextureDesc {
          .width = request.resolution,
          .height = request.resolution,
          .array_size = request.layers,
          .format = format,
          .texture_type = TextureType::kTexture2DArray,
          .is_shader_resource = true,
          .is_render_target = true,
          .is_typeless = true,
        });
      EXPECT_EQ(descriptor.Format,
        format == Format::kDepth32 ? DXGI_FORMAT_R32_TYPELESS
                                   : DXGI_FORMAT_R32G8X24_TYPELESS);
      EXPECT_EQ(descriptor.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
      const auto allocation
        = device->GetResourceAllocationInfo(0U, 1U, &descriptor);
      ASSERT_NE(
        allocation.SizeInBytes, std::numeric_limits<std::uint64_t>::max());
      ASSERT_GT(allocation.SizeInBytes, 0U);
      ASSERT_GT(allocation.Alignment, 0U);
      EXPECT_EQ(allocation.SizeInBytes % allocation.Alignment, 0U);
      report.at("requirements")
        .push_back({
          { "name", request.name },
          { "format", format_name },
          { "resolution", request.resolution },
          { "layers", request.layers },
          { "size_bytes", allocation.SizeInBytes },
          { "alignment_bytes", allocation.Alignment },
        });
    }
  }
  RecordProperty("shadow_allocation_requirements", report.dump());
}

} // namespace oxygen::vortex::testing
