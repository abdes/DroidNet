//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageFlagPropagationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageInitializationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace oxygen::renderer::vsm::testing {

struct StageMeshVertex {
  glm::vec3 position {};
  glm::vec3 normal {};
  glm::vec2 texcoord {};
  glm::vec3 tangent {};
  glm::vec3 bitangent {};
  glm::vec4 color {};
};
static_assert(sizeof(StageMeshVertex) == 72U);

struct StageShaderVisibleBuffer {
  std::shared_ptr<oxygen::graphics::Buffer> buffer {};
  oxygen::ShaderVisibleIndex slot { oxygen::kInvalidShaderVisibleIndex };
};

class VsmStageGpuHarness : public VsmCacheManagerGpuTestBase {
protected:
  using VsmCacheManagerGpuTestBase::ReadBufferBytes;

  static constexpr std::uint32_t kTextureUploadRowAlignment = 256U;
  static constexpr oxygen::ViewId kTestViewId { 11U };
  static constexpr std::uint32_t kSingleTexelReadbackRowPitch = 256U;

  [[nodiscard]] static auto AlignUp(
    const std::uint32_t value, const std::uint32_t alignment) -> std::uint32_t
  {
    CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
    return (value + alignment - 1U) / alignment * alignment;
  }

  [[nodiscard]] static auto MakeResolvedView() -> oxygen::ResolvedView
  {
    return MakeResolvedView(1U, 1U);
  }

  [[nodiscard]] static auto MakeResolvedView(const std::uint32_t width,
    const std::uint32_t height) -> oxygen::ResolvedView
  {
    auto view_config = oxygen::View {};
    view_config.viewport = oxygen::ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = oxygen::Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(width),
      .bottom = static_cast<std::int32_t>(height),
    };

    return oxygen::ResolvedView(oxygen::ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = glm::mat4 { 1.0F },
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = oxygen::NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeSingleSliceShadowPoolConfig(
    const std::uint32_t page_size_texels,
    const std::uint32_t physical_tile_capacity, const char* debug_name)
    -> oxygen::renderer::vsm::VsmPhysicalPoolConfig
  {
    return oxygen::renderer::vsm::VsmPhysicalPoolConfig {
      .page_size_texels = page_size_texels,
      .physical_tile_capacity = physical_tile_capacity,
      .array_slice_count = 1U,
      .depth_format = oxygen::Format::kDepth32,
      .slice_roles
      = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth },
      .debug_name = debug_name,
    };
  }

  [[nodiscard]] static auto MakeCustomLocalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const std::initializer_list<std::string_view> remap_keys,
    const char* frame_name = "vsm-frame")
    -> oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);

    std::uint32_t light_index = 0U;
    for (const auto remap_key : remap_keys) {
      const auto suffix = std::to_string(light_index++);
      address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
        .remap_key = std::string(remap_key),
        .debug_name = "local-" + suffix,
      });
    }

    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeMultiLevelLocalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const std::string_view remap_key, const std::uint32_t level_count,
    const std::uint32_t pages_x, const std::uint32_t pages_y,
    const char* frame_name = "vsm-multi-level-frame")
    -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);
    address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = std::string(remap_key),
      .level_count = level_count,
      .pages_per_level_x = pages_x,
      .pages_per_level_y = pages_y,
      .debug_name = std::string(remap_key),
    });
    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakePageRequests(const std::uint32_t map_id,
    std::initializer_list<VsmVirtualPageCoord> pages)
    -> std::vector<VsmPageRequest>
  {
    auto requests = std::vector<VsmPageRequest> {};
    requests.reserve(pages.size());
    for (const auto& page : pages) {
      requests.push_back(VsmPageRequest {
        .map_id = map_id,
        .page = page,
      });
    }
    return requests;
  }

  template <typename TLayout>
  [[nodiscard]] static auto ResolvePageTableEntryIndex(
    const TLayout& layout, const VsmVirtualPageCoord& page) -> std::uint32_t
  {
    const auto entry_index = TryGetPageTableEntryIndex(layout, page);
    EXPECT_TRUE(entry_index.has_value());
    return entry_index.value_or(0U);
  }

  static auto CommitFrame(VsmCacheManager& manager)
    -> const VsmPageAllocationFrame&
  {
    static_cast<void>(manager.BuildPageAllocationPlan());
    return manager.CommitPageAllocationFrame();
  }

  auto ExecutePageManagementPass(const VsmPageAllocationFrame& frame,
    const oxygen::engine::VsmPageManagementFinalStage final_stage,
    std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = oxygen::engine::VsmPageManagementPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<oxygen::engine::VsmPageManagementPassConfig>(
        oxygen::engine::VsmPageManagementPassConfig {
          .final_stage = final_stage,
          .debug_name = std::string(debug_name),
        }));
    pass.SetFrameInput(frame);

    auto prepared_frame = oxygen::engine::PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
    return pass.GetAvailablePageCountBuffer();
  }

  auto ExecutePropagationPass(
    const VsmPageAllocationFrame& frame, std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = oxygen::engine::VsmPageFlagPropagationPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<oxygen::engine::VsmPageFlagPropagationPassConfig>(
        oxygen::engine::VsmPageFlagPropagationPassConfig {
          .debug_name = std::string(debug_name),
        }));
    pass.SetFrameInput(frame);

    auto prepared_frame = oxygen::engine::PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }

  auto ExecuteInitializationPass(
    const oxygen::engine::VsmPageInitializationPassInput& input,
    std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = oxygen::engine::VsmPageInitializationPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<oxygen::engine::VsmPageInitializationPassConfig>(
        oxygen::engine::VsmPageInitializationPassConfig {
          .debug_name = std::string(debug_name),
        }));
    pass.SetInput(input);

    auto prepared_frame = oxygen::engine::PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }

  template <typename T>
  auto ReadBufferAs(
    const std::shared_ptr<const oxygen::graphics::Buffer>& buffer,
    const std::size_t element_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto bytes = ReadBufferBytes(
      std::const_pointer_cast<oxygen::graphics::Buffer>(buffer),
      element_count * sizeof(T), debug_name);
    auto result = std::vector<T>(element_count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
  }

  auto ReadBufferBytes(const oxygen::graphics::Buffer& source,
    const std::size_t size_bytes, std::string_view debug_name)
    -> std::vector<std::byte>
  {
    auto readback = GetReadbackManager()->CreateBufferReadback(debug_name);
    CHECK_NOTNULL_F(readback.get(), "Failed to create buffer readback");

    oxygen::graphics::ReadbackTicket ticket {};
    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire readback recorder");
      recorder->BeginTrackingResourceState(
        source, oxygen::graphics::ResourceStates::kCommon, true);

      const auto queued_ticket = readback->EnqueueCopy(
        *recorder, source, oxygen::graphics::BufferRange { 0U, size_bytes });
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

  template <typename T>
  auto ReadBufferAs(const oxygen::graphics::Buffer& source,
    const std::size_t element_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto bytes
      = ReadBufferBytes(source, sizeof(T) * element_count, debug_name);
    auto result = std::vector<T>(element_count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
  }

  auto CreateMetadataSeedBuffer(
    const std::span<const VsmPhysicalPageMeta> metadata,
    std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(metadata.size())
        * sizeof(VsmPhysicalPageMeta),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create metadata seed buffer");
    UploadBufferBytes(buffer, metadata.data(),
      metadata.size() * sizeof(VsmPhysicalPageMeta), debug_name);
    return buffer;
  }

  auto CreateDepthTexture2D(const std::uint32_t width,
    const std::uint32_t height, std::string_view debug_name)
    -> std::shared_ptr<oxygen::graphics::Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = oxygen::Format::kDepth32;
    texture_desc.texture_type = oxygen::TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = oxygen::graphics::ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  auto CreateSingleChannelTexture2D(const std::uint32_t width,
    const std::uint32_t height, const oxygen::Format format,
    std::string_view debug_name, const bool is_typeless = false)
    -> std::shared_ptr<oxygen::graphics::Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = format;
    texture_desc.texture_type = oxygen::TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_typeless = is_typeless;
    texture_desc.initial_state = oxygen::graphics::ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  auto UploadDepthTexture(
    const std::shared_ptr<oxygen::graphics::Texture>& depth_texture,
    const float depth_value, std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(depth_texture.get(), "Cannot upload into a null texture");

    const auto width = depth_texture->GetDescriptor().width;
    const auto height = depth_texture->GetDescriptor().height;
    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(kTextureUploadRowAlignment) * height,
      std::byte { 0 });

    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * kTextureUploadRowAlignment
            + static_cast<std::size_t>(x) * sizeof(float),
          &depth_value, sizeof(depth_value));
      }
    }

    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(upload_bytes.size()),
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create upload buffer");
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".SeedDepth");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire upload recorder");
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(
        *recorder, depth_texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *depth_texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = kTextureUploadRowAlignment,
          .buffer_slice_pitch = kTextureUploadRowAlignment * height,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = width,
            .height = height,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *depth_texture);
      recorder->RequireResourceStateFinal(
        *depth_texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto UploadFloatTexture2D(
    const std::shared_ptr<oxygen::graphics::Texture>& texture,
    const std::span<const float> values, std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot upload into a null texture");

    const auto width = texture->GetDescriptor().width;
    const auto height = texture->GetDescriptor().height;
    CHECK_EQ_F(values.size(), static_cast<std::size_t>(width) * height,
      "Float texture upload size must match the texture extent");

    const auto row_pitch
      = AlignUp(width * static_cast<std::uint32_t>(sizeof(float)),
        kTextureUploadRowAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create upload buffer");

    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(row_pitch) * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        const auto value = values[static_cast<std::size_t>(y) * width + x];
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * row_pitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Upload");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire upload recorder");
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(
        *recorder, texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = row_pitch,
          .buffer_slice_pitch = row_pitch * height,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = width,
            .height = height,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *texture);
      recorder->RequireResourceStateFinal(
        *texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto CreateUploadedFloatTexture2D(const std::span<const float> values,
    const std::uint32_t width, const std::uint32_t height,
    const oxygen::Format format, std::string_view debug_name,
    const bool is_typeless = false)
    -> std::shared_ptr<oxygen::graphics::Texture>
  {
    auto texture = CreateSingleChannelTexture2D(
      width, height, format, debug_name, is_typeless);
    UploadFloatTexture2D(texture, values, debug_name);
    return texture;
  }

  template <typename T>
  auto CreateStructuredSrvBuffer(std::span<const T> elements,
    std::string_view debug_name) -> StageShaderVisibleBuffer
  {
    CHECK_F(
      !elements.empty(), "Structured SRV buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);

    UploadBufferBytes(
      buffer, elements.data(), elements.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(
      oxygen::graphics::ResourceViewType::kStructuredBuffer_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate structured SRV for `{}`",
      debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = oxygen::graphics::ResourceViewType::kStructuredBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .range = oxygen::graphics::BufferRange {
        0U,
        static_cast<std::uint64_t>(elements.size_bytes()),
      },
      .stride = static_cast<std::uint32_t>(sizeof(T)),
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register structured SRV for `{}`",
      debug_name);

    return StageShaderVisibleBuffer {
      .buffer = std::move(buffer),
      .slot = slot,
    };
  }

  auto CreateUIntIndexBuffer(std::span<const std::uint32_t> indices,
    std::string_view debug_name) -> StageShaderVisibleBuffer
  {
    CHECK_F(!indices.empty(), "Index buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(indices.size_bytes()),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);

    UploadBufferBytes(buffer, indices.data(), indices.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle
      = allocator.Allocate(oxygen::graphics::ResourceViewType::kRawBuffer_SRV,
        oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(
      handle.IsValid(), "Failed to allocate typed SRV for `{}`", debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = oxygen::graphics::ResourceViewType::kRawBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .format = oxygen::Format::kR32UInt,
      .range = oxygen::graphics::BufferRange {
        0U,
        static_cast<std::uint64_t>(indices.size_bytes()),
      },
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(
      view->IsValid(), "Failed to register typed SRV for `{}`", debug_name);

    return StageShaderVisibleBuffer {
      .buffer = std::move(buffer),
      .slot = slot,
    };
  }

  template <typename T>
  auto CreateStructuredStorageBuffer(std::span<const T> elements,
    std::string_view debug_name) -> std::shared_ptr<oxygen::graphics::Buffer>
  {
    CHECK_F(!elements.empty(),
      "Structured storage buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(
      buffer.get(), "Failed to create storage buffer `{}`", debug_name);
    UploadBufferBytes(
      buffer, elements.data(), elements.size_bytes(), debug_name);
    return buffer;
  }

  auto ClearShadowSlice(
    const std::shared_ptr<const oxygen::graphics::Texture>& shadow_texture,
    const std::uint32_t array_slice, const float depth_value,
    std::string_view debug_name) -> void
  {
    auto texture
      = std::const_pointer_cast<oxygen::graphics::Texture>(shadow_texture);
    CHECK_NOTNULL_F(texture.get(), "Cannot clear a null shadow texture");
    CHECK_F(Backend().GetResourceRegistry().Contains(*texture),
      "Shadow pool texture must be registered before test DSV creation");

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle
      = allocator.Allocate(oxygen::graphics::ResourceViewType::kTexture_DSV,
        oxygen::graphics::DescriptorVisibility::kCpuOnly);
    CHECK_F(handle.IsValid(), "Failed to allocate DSV for `{}`", debug_name);

    const auto dsv_desc = oxygen::graphics::TextureViewDescription {
      .view_type = oxygen::graphics::ResourceViewType::kTexture_DSV,
      .visibility = oxygen::graphics::DescriptorVisibility::kCpuOnly,
      .format = texture->GetDescriptor().format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = array_slice,
        .num_array_slices = 1U,
      },
      .is_read_only_dsv = false,
    };
    auto dsv = Backend().GetResourceRegistry().RegisterView(
      *texture, std::move(handle), dsv_desc);
    CHECK_F(dsv->IsValid(), "Failed to register DSV for `{}`", debug_name);

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(
        *recorder, texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearDepthStencilView(
        *texture, dsv, oxygen::graphics::ClearFlags::kDepth, depth_value, 0U);
      recorder->RequireResourceStateFinal(
        *texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto ReadDepthTexel(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t array_slice, const std::uint32_t x,
    const std::uint32_t y, std::string_view debug_name) -> float
  {
    auto writable_texture
      = std::const_pointer_cast<oxygen::graphics::Texture>(texture);
    CHECK_NOTNULL_F(writable_texture.get(), "Cannot read a null texture");

    auto readback = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = kSingleTexelReadbackRowPitch,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire probe recorder");
      EnsureTracked(*recorder, writable_texture,
        oxygen::graphics::ResourceStates::kShaderResource);
      EnsureTracked(
        *recorder, readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->RequireResourceState(
        *writable_texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *writable_texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch
          = oxygen::SizeBytes { kSingleTexelReadbackRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = array_slice,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped = static_cast<const std::byte*>(
      readback->Map(0U, kSingleTexelReadbackRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  auto ReadTextureMipTexel(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t mip_level, const std::uint32_t x, const std::uint32_t y,
    std::string_view debug_name, const std::uint32_t array_slice = 0U) -> float
  {
    auto writable_texture
      = std::const_pointer_cast<oxygen::graphics::Texture>(texture);
    CHECK_NOTNULL_F(writable_texture.get(), "Cannot read a null texture");

    auto readback = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = kSingleTexelReadbackRowPitch,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire probe recorder");
      EnsureTracked(*recorder, writable_texture,
        oxygen::graphics::ResourceStates::kShaderResource);
      EnsureTracked(
        *recorder, readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->RequireResourceState(
        *writable_texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *writable_texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch
          = oxygen::SizeBytes { kSingleTexelReadbackRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = mip_level,
            .array_slice = array_slice,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped = static_cast<const std::byte*>(
      readback->Map(0U, kSingleTexelReadbackRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  auto AttachRasterOutputBuffers(VsmPageAllocationFrame& frame,
    const std::uint64_t frame_generation, const std::size_t physical_page_count,
    std::string_view debug_name,
    std::vector<VsmPhysicalPageMeta> initial_meta = {},
    std::vector<std::uint32_t> initial_dirty_flags = {}) -> void
  {
    CHECK_F(physical_page_count != 0U,
      "Raster output buffers require at least one physical page");
    if (initial_meta.empty()) {
      initial_meta.resize(physical_page_count);
    }
    if (initial_dirty_flags.empty()) {
      initial_dirty_flags.resize(physical_page_count, 0U);
    }
    CHECK_F(initial_meta.size() == physical_page_count,
      "Initial physical-page metadata size mismatch");
    CHECK_F(initial_dirty_flags.size() == physical_page_count,
      "Initial dirty-flags size mismatch");

    frame.snapshot.frame_generation = frame_generation;
    frame.snapshot.physical_pages = initial_meta;
    frame.physical_page_meta_buffer = CreateStructuredStorageBuffer(
      std::span<const VsmPhysicalPageMeta>(frame.snapshot.physical_pages.data(),
        frame.snapshot.physical_pages.size()),
      std::string(debug_name) + ".PhysicalMeta");
    frame.dirty_flags_buffer = CreateStructuredStorageBuffer(
      std::span<const std::uint32_t>(
        initial_dirty_flags.data(), initial_dirty_flags.size()),
      std::string(debug_name) + ".DirtyFlags");
  }

  [[nodiscard]] static auto MakeBaseViewConstants(
    const oxygen::ShaderVisibleIndex view_frame_slot,
    const oxygen::frame::Slot frame_slot,
    const oxygen::frame::SequenceNumber frame_sequence)
    -> oxygen::engine::ViewConstants::GpuData
  {
    auto base_view_constants = oxygen::engine::ViewConstants {};
    base_view_constants
      .SetTimeSeconds(0.0F, oxygen::engine::ViewConstants::kRenderer)
      .SetFrameSlot(frame_slot, oxygen::engine::ViewConstants::kRenderer)
      .SetFrameSequenceNumber(
        frame_sequence, oxygen::engine::ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        oxygen::engine::BindlessViewFrameBindingsSlot(view_frame_slot),
        oxygen::engine::ViewConstants::kRenderer);
    return base_view_constants.GetSnapshot();
  }

  auto CreateFilledSingleChannelTexture(const std::uint32_t width,
    const std::uint32_t height, const float value, std::string_view debug_name)
    -> std::shared_ptr<oxygen::graphics::Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = oxygen::Format::kR32Float;
    texture_desc.texture_type = oxygen::TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.debug_name = std::string(debug_name);

    auto texture = CreateRegisteredTexture(texture_desc);
    CHECK_NOTNULL_F(texture.get(), "Failed to create single-channel texture");

    const auto row_pitch
      = AlignUp(width * sizeof(float), kTextureUploadRowAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "_Upload",
    });
    CHECK_NOTNULL_F(
      upload.get(), "Failed to create single-channel upload buffer");

    auto upload_bytes
      = std::vector<std::byte>(row_pitch * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        std::memcpy(upload_bytes.data() + y * row_pitch + x * sizeof(float),
          &value, sizeof(float));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + "_Upload");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(
        *recorder, texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = row_pitch,
          .buffer_slice_pitch = row_pitch * height,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = width,
            .height = height,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *texture);
      recorder->RequireResourceStateFinal(
        *texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  auto SeedShadowPageValue(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t page_size, const std::uint32_t tile_x,
    const std::uint32_t tile_y, const std::uint32_t slice, const float value,
    std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot seed a null shadow texture");

    auto seed_texture = CreateFilledSingleChannelTexture(
      page_size, page_size, value, std::string(debug_name) + ".Seed");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Copy");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, seed_texture, oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(*recorder,
        std::const_pointer_cast<oxygen::graphics::Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *seed_texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTexture(*seed_texture,
        oxygen::graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        },
        *std::const_pointer_cast<oxygen::graphics::Texture>(texture),
        oxygen::graphics::TextureSlice {
          .x = tile_x * page_size,
          .y = tile_y * page_size,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = slice,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = slice,
          .num_array_slices = 1U,
        });
    }
    WaitForQueueIdle();
  }

  [[nodiscard]] auto ReadShadowDepthTexel(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, const std::uint32_t slice,
    std::string_view debug_name) -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null shadow texture");

    auto probe_desc = oxygen::graphics::TextureDesc {};
    probe_desc.width = 1U;
    probe_desc.height = 1U;
    probe_desc.format = oxygen::Format::kR32Float;
    probe_desc.texture_type = oxygen::TextureType::kTexture2D;
    probe_desc.is_shader_resource = true;
    probe_desc.debug_name = std::string(debug_name) + ".Probe";

    auto probe = CreateRegisteredTexture(probe_desc);
    CHECK_NOTNULL_F(probe.get(), "Failed to create probe texture");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".ProbeCopy");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder,
        std::const_pointer_cast<oxygen::graphics::Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(
        *recorder, probe, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *probe, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTexture(*texture,
        oxygen::graphics::TextureSlice {
          .x = x,
          .y = y,
          .z = 0U,
          .width = 1U,
          .height = 1U,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = slice,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = slice,
          .num_array_slices = 1U,
        },
        *probe,
        oxygen::graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = 1U,
          .height = 1U,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        });
      recorder->RequireResourceStateFinal(
        *probe, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();

    const auto readback
      = GetReadbackManager()->ReadTextureNow(*probe, {}, true);
    CHECK_F(readback.has_value(), "Texture readback failed");
    CHECK_F(readback->bytes.size() >= sizeof(float),
      "Texture readback returned too few bytes");
    float value = 0.0F;
    std::memcpy(&value, readback->bytes.data(), sizeof(float));
    return value;
  }
};

} // namespace oxygen::renderer::vsm::testing
