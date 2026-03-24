//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

namespace oxygen::renderer::vsm::testing {

class VirtualShadowGpuTest
  : public oxygen::graphics::d3d12::testing::ReadbackTestFixture {
protected:
  using EventLoop = oxygen::co::testing::TestEventLoop;

  [[nodiscard]] auto PathFinderConfigJson() const -> std::string override
  {
    auto cursor = std::filesystem::current_path();
    constexpr auto kShaderCatalogSentinel
      = "src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h";

    for (;;) {
      if (std::filesystem::exists(cursor / kShaderCatalogSentinel)) {
        const auto config = std::string { R"({"workspace_root_path":")" }
          + (cursor.generic_string()) + R"("})";
        DLOG_F(2, "VirtualShadowGpuTest PathFinderConfigJson={}", config);
        return config;
      }
      if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
        break;
      }
      cursor = cursor.parent_path();
    }

    CHECK_F(false,
      "Failed to locate Oxygen workspace root for shader-backed VSM GPU tests "
      "starting from `{}`",
      std::filesystem::current_path().string());
  }

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

  [[nodiscard]] auto MakeRendererConfig() const -> oxygen::RendererConfig
  {
    return oxygen::RendererConfig {
      .upload_queue_key = QueueKeyFor().get(),
    };
  }

  [[nodiscard]] auto MakeRenderer() const
    -> std::unique_ptr<oxygen::engine::Renderer>
  {
    return std::make_unique<oxygen::engine::Renderer>(
      GetGraphicsShared(), MakeRendererConfig());
  }

  static auto RunPass(oxygen::engine::RenderPass& pass,
    const oxygen::engine::RenderContext& render_context,
    oxygen::graphics::CommandRecorder& recorder) -> void
  {
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      co_await pass.PrepareResources(render_context, recorder);
      co_await pass.Execute(render_context, recorder);
    });
  }

  auto ReadBufferBytes(const std::shared_ptr<oxygen::graphics::Buffer>& source,
    const std::size_t size_bytes, std::string_view debug_name)
    -> std::vector<std::byte>
  {
    CHECK_NOTNULL_F(source.get(), "Cannot read back a null buffer");

    auto readback = GetReadbackManager()->CreateBufferReadback(debug_name);
    CHECK_NOTNULL_F(readback.get(), "Failed to create buffer readback");

    oxygen::graphics::ReadbackTicket ticket {};
    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, source, oxygen::graphics::ResourceStates::kCommon);

      const auto queued_ticket = readback->EnqueueCopy(
        *recorder, *source, oxygen::graphics::BufferRange { 0U, size_bytes });
      CHECK_F(queued_ticket.has_value(), "Buffer readback enqueue failed");
      ticket = *queued_ticket;
    }

    const auto result = AwaitReadback(ticket);
    CHECK_F(result.has_value(), "Buffer readback await failed");

    const auto mapped = readback->TryMap();
    CHECK_F(mapped.has_value(), "Buffer readback map failed");
    const auto bytes = mapped->Bytes();
    return { bytes.begin(), bytes.end() };
  }

  auto UploadBufferBytes(const std::shared_ptr<oxygen::graphics::Buffer>& dest,
    const void* data, const std::size_t size_bytes, std::string_view debug_name,
    const oxygen::graphics::ResourceStates initial_state
    = oxygen::graphics::ResourceStates::kCommon,
    const oxygen::graphics::ResourceStates final_state
    = oxygen::graphics::ResourceStates::kCommon) -> void
  {
    CHECK_NOTNULL_F(dest.get(), "Cannot upload into a null buffer");
    CHECK_NOTNULL_F(data, "Cannot upload from a null source pointer");

    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(size_bytes),
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "_Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create upload buffer");

    upload->Update(data, size_bytes, 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(*recorder, dest, initial_state);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *dest, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*dest, 0U, *upload, 0U, size_bytes);
      recorder->RequireResourceStateFinal(*dest, final_state);
    }

    WaitForQueueIdle();
  }
};

class VsmCacheManagerGpuTestBase : public VirtualShadowGpuTest {
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
};

} // namespace oxygen::renderer::vsm::testing
