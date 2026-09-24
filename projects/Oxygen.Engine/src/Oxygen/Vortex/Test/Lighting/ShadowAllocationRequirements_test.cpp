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
#include <vector>

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

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
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
    { "shadow_resources_allocated", true },
    {
      "scope",
      "native D32 allocator chunks and budget accounting; not whole-frame peak",
    },
    { "requirements", nlohmann::json::array() },
  };
  auto config = RendererConfig {};
  config.upload_queue_key
    = Backend().QueueKeyFor(graphics::QueueRole::kGraphics).get();
  auto renderer = Renderer(
    GetGraphicsShared(), config, RendererCapabilityFamily::kLightingData);
  const auto shutdown = ScopeGuard([&]() noexcept { renderer.OnShutdown(); });
  const auto budget = renderer.GetLightingAllocationBudget();
  const auto baseline = budget->Snapshot().allocated.get();
  Backend().BeginFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  {
    using Allocator = shadows::internal::ConventionalShadowTargetAllocator;
    auto allocator = Allocator(renderer);
    allocator.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    const auto view = ViewId { 81U };
    enum class Kind { kDirectional, kPoint, kSpot };
    struct Request {
      const char* name;
      Kind kind;
      std::uint32_t resolution;
      std::uint32_t requested_count;
      std::uint32_t chunk;
      std::uint32_t expected_layers;
    };
    constexpr auto requests = std::array {
      Request { "directional", Kind::kDirectional, 1024U, 2U, 0U, 2U },
      Request { "low_point", Kind::kPoint, 512U, 2U, 0U, 60U },
      Request { "medium_point", Kind::kPoint, 1024U, 2U, 0U, 12U },
      Request { "appended_point", Kind::kPoint, 1024U, 1U, 1U, 12U },
      Request { "low_spot", Kind::kSpot, 512U, 2U, 0U, 64U },
      Request { "medium_spot", Kind::kSpot, 1024U, 1U, 0U, 16U },
    };
    auto total_native_bytes = std::uint64_t { 0U };
    auto surfaces = std::vector<std::shared_ptr<graphics::Texture>> {};
    for (const auto& request : requests) {
      SCOPED_TRACE(request.name);
      const auto acquire = [&]() -> std::shared_ptr<graphics::Texture> {
        switch (request.kind) {
        case Kind::kDirectional:
          return allocator
            .AcquireDirectionalSurface(view, LightSelectionIndex { 0U },
              request.requested_count, scene::ShadowResolutionHint::kLow)
            .surface;
        case Kind::kPoint:
          return allocator
            .AcquirePointSurface(
              view, request.requested_count, request.resolution, request.chunk)
            .surface;
        case Kind::kSpot:
          return allocator
            .AcquireSpotSurface(
              view, request.requested_count, request.resolution, request.chunk)
            .surface;
        }
        return {};
      };
      auto surface = acquire();
      ASSERT_NE(surface, nullptr);
      const auto& logical = surface->GetDescriptor();
      EXPECT_EQ(logical.format, Format::kDepth32);
      EXPECT_EQ(logical.width, request.resolution);
      EXPECT_EQ(logical.height, request.resolution);
      EXPECT_EQ(logical.array_size, request.expected_layers);
      EXPECT_EQ(logical.texture_type,
        request.kind == Kind::kPoint ? TextureType::kTextureCubeArray
                                     : TextureType::kTexture2DArray);
      const auto native
        = surface->GetNativeResource()->AsPointer<ID3D12Resource>()->GetDesc();
      EXPECT_EQ(native.Format, DXGI_FORMAT_R32_TYPELESS);
      EXPECT_EQ(native.Width, request.resolution);
      EXPECT_EQ(native.Height, request.resolution);
      EXPECT_EQ(native.DepthOrArraySize, request.expected_layers);
      EXPECT_EQ(native.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
      const auto allocation
        = device->GetResourceAllocationInfo(0U, 1U, &native);
      ASSERT_NE(
        allocation.SizeInBytes, std::numeric_limits<std::uint64_t>::max());
      ASSERT_GT(allocation.Alignment, 0U);
      EXPECT_GE(allocation.SizeInBytes,
        std::uint64_t { request.resolution } * request.resolution
          * request.expected_layers * sizeof(float));
      EXPECT_EQ(allocation.SizeInBytes % allocation.Alignment, 0U);
      total_native_bytes += allocation.SizeInBytes;
      EXPECT_EQ(
        budget->Snapshot().allocated.get() - baseline, total_native_bytes);
      // Reuse must not allocate or replace the chunk, including after append.
      EXPECT_EQ(acquire(), surface);
      EXPECT_EQ(
        budget->Snapshot().allocated.get() - baseline, total_native_bytes);
      for (const auto& prior : surfaces) {
        EXPECT_NE(prior, surface);
      }
      surfaces.push_back(std::move(surface));
      report.at("requirements")
        .push_back({ { "name", request.name }, { "format", "D32" },
          { "resolution", request.resolution },
          { "layers", native.DepthOrArraySize },
          { "size_bytes", allocation.SizeInBytes },
          { "alignment_bytes", allocation.Alignment } });
    }
    report["charged_shadow_bytes"] = total_native_bytes;
  }
  Backend().GetDeferredReclaimer().ProcessAllDeferredReleases();
  EXPECT_EQ(budget->Snapshot().allocated.get(), baseline);
  Backend().EndFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  RecordProperty("shadow_allocation_requirements", report.dump());
}

} // namespace oxygen::vortex::testing
