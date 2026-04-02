//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowCasterCullingPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>
#include <Oxygen/Renderer/Test/Fixtures/SyntheticSceneBuilder.h>
#include <Oxygen/Renderer/Types/ConventionalShadowIndirectDrawCommand.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::observer_ptr;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::ConventionalShadowCasterCullingPass;
using oxygen::engine::ConventionalShadowCasterCullingPassConfig;
using oxygen::engine::ConventionalShadowReceiverAnalysisPass;
using oxygen::engine::ConventionalShadowReceiverAnalysisPassConfig;
using oxygen::engine::ConventionalShadowReceiverMaskPass;
using oxygen::engine::ConventionalShadowReceiverMaskPassConfig;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::ScreenHzbBuildPassConfig;
using oxygen::engine::ViewConstants;
using oxygen::engine::internal::BuildConventionalShadowDrawRecords;
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::engine::testing::SyntheticSceneBuilder;
using oxygen::engine::testing::TestVertex;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::renderer::ConventionalShadowIndirectDrawCommand;
using oxygen::renderer::ConventionalShadowReceiverAnalysisJob;
using oxygen::renderer::ConventionalShadowReceiverAnalysisPlan;
using oxygen::renderer::ConventionalShadowReceiverMaskSummary;
using oxygen::renderer::LightManager;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

constexpr ViewId kTestViewId { 39U };
constexpr std::uint32_t kWidth = 16U;
constexpr std::uint32_t kHeight = 16U;
constexpr std::uint32_t kUploadRowPitch = 256U;

class ConventionalShadowCasterCullingPassTest
  : public RendererOffscreenGpuTestFixture {
protected:
  template <typename T> struct UploadedStructuredBuffer {
    std::shared_ptr<Buffer> buffer {};
    ShaderVisibleIndex slot { oxygen::kInvalidShaderVisibleIndex };
  };

  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(kWidth),
      .height = static_cast<float>(kHeight),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(kWidth),
      .bottom = static_cast<std::int32_t>(kHeight),
    };

    const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
        glm::radians(75.0F), 1.0F, 0.1F, 100.0F);

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = view_matrix,
      .proj_matrix = projection_matrix,
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeViewConstants(const ResolvedView& resolved_view,
    const Slot frame_slot, const SequenceNumber frame_sequence) -> ViewConstants
  {
    auto view_constants = ViewConstants {};
    view_constants.SetFrameSlot(frame_slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(frame_sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        oxygen::engine::BindlessViewFrameBindingsSlot {
          oxygen::ShaderVisibleIndex { 1U } },
        ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer)
      .SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetStableProjectionMatrix(resolved_view.StableProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition());
    return view_constants;
  }

  auto CreateDepthTexture(std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = kWidth;
    texture_desc.height = kHeight;
    texture_desc.format = Format::kDepth32;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  [[nodiscard]] static auto DeviceDepthFromLinearViewDepth(
    const float linear_depth, const glm::mat4& projection_matrix) -> float
  {
    const auto clip
      = projection_matrix * glm::vec4(0.0F, 0.0F, -linear_depth, 1.0F);
    return clip.z / std::max(clip.w, 1.0e-6F);
  }

  auto SeedDepthPatch(const std::shared_ptr<Texture>& depth_texture,
    const ResolvedView& resolved_view, std::string_view debug_name) -> void
  {
    ASSERT_NE(depth_texture, nullptr);
    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(kUploadRowPitch) * kHeight, std::byte { 0 });

    const float near_depth
      = DeviceDepthFromLinearViewDepth(1.25F, resolved_view.ProjectionMatrix());
    const float mid_depth
      = DeviceDepthFromLinearViewDepth(3.50F, resolved_view.ProjectionMatrix());

    for (std::uint32_t y = 5U; y < 11U; ++y) {
      for (std::uint32_t x = 6U; x < 10U; ++x) {
        const float value = y < 8U ? near_depth : mid_depth;
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * kUploadRowPitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }

    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(upload_bytes.size()),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    ASSERT_NE(upload, nullptr);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".SeedDepth");
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*depth_texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = kUploadRowPitch,
          .buffer_slice_pitch = kUploadRowPitch * kHeight,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = kWidth,
            .height = kHeight,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *depth_texture);
      recorder->RequireResourceStateFinal(
        *depth_texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  template <typename T>
  auto UploadStructuredSrvBuffer(std::span<const T> elements,
    std::string_view debug_name) -> UploadedStructuredBuffer<T>
  {
    CHECK_F(!elements.empty(), "UploadStructuredSrvBuffer requires elements");

    auto device_buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(device_buffer.get(), "Failed to create `{}`", debug_name);

    auto upload_buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload_buffer.get(),
      "Failed to create upload buffer for `{}`", debug_name);
    upload_buffer->Update(elements.data(), elements.size_bytes(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Upload");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire upload recorder");
      EnsureTracked(*recorder, upload_buffer, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, device_buffer, ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload_buffer, ResourceStates::kCopySource);
      recorder->RequireResourceState(*device_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(
        *device_buffer, 0U, *upload_buffer, 0U, elements.size_bytes());
      recorder->RequireResourceStateFinal(
        *device_buffer, ResourceStates::kCommon);
    }
    WaitForQueueIdle();

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate SRV for `{}`", debug_name);
    const auto slot = allocator.GetShaderVisibleIndex(handle);
    auto view = Backend().GetResourceRegistry().RegisterView(*device_buffer,
      std::move(handle),
      oxygen::graphics::BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .visibility = DescriptorVisibility::kShaderVisible,
        .range = { 0U, static_cast<std::uint64_t>(elements.size_bytes()) },
        .stride = static_cast<std::uint32_t>(sizeof(T)),
      });
    CHECK_F(view->IsValid(), "Failed to register SRV for `{}`", debug_name);

    return UploadedStructuredBuffer<T> {
      .buffer = std::move(device_buffer),
      .slot = slot,
    };
  }

  [[nodiscard]] static auto MakeShadowCasterMask(const bool masked) -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kShadowCaster);
    mask.Set(masked ? PassMaskBit::kMasked : PassMaskBit::kOpaque);
    return mask;
  }

  [[nodiscard]] static auto MakeTestVertex(
    const float x, const float y, const float z) -> TestVertex
  {
    return TestVertex {
      .position = { x, y, z },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    };
  }

  [[nodiscard]] static auto CreateShadowCastingDirectionalNode(Scene& scene)
    -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(true));

    auto node = scene.CreateNode("caster-culling.directional", flags);
    EXPECT_TRUE(node.IsValid());

    auto impl = node.GetImpl();
    EXPECT_TRUE(impl.has_value());
    if (impl.has_value()) {
      impl->get().AddComponent<DirectionalLight>();
      impl->get().GetComponent<DirectionalLight>().Common().casts_shadows
        = true;
      impl->get().UpdateTransforms(scene);
    }

    return node;
  }

  [[nodiscard]] static auto BuildRejectedWorldSphere(
    const ConventionalShadowReceiverAnalysisJob& job,
    const ConventionalShadowReceiverMaskSummary& summary) -> glm::vec4
  {
    const auto radius = std::max(0.01F,
      0.05F
        * std::max(summary.raw_xy_min_max.z - summary.raw_xy_min_max.x,
          summary.raw_depth_and_dilation.y - summary.raw_depth_and_dilation.x));
    const auto sphere_center_ls = glm::vec3 {
      summary.raw_xy_min_max.z + summary.raw_depth_and_dilation.z + radius
        + 0.25F,
      0.5F * (summary.raw_xy_min_max.y + summary.raw_xy_min_max.w),
      0.5F
        * (summary.raw_depth_and_dilation.x + summary.raw_depth_and_dilation.y),
    };

    const auto inverse_light_rotation = glm::inverse(job.light_rotation_matrix);
    const auto center_ws
      = inverse_light_rotation * glm::vec4(sphere_center_ls, 1.0F);
    return glm::vec4(center_ws.x, center_ws.y, center_ws.z, radius);
  }

  [[nodiscard]] static auto ComputeSphereTileBounds(const glm::vec2 center_xy,
    const float radius, const glm::vec4& full_rect_center_half_extent,
    const std::uint32_t base_tile_resolution,
    std::pair<std::uint32_t, std::uint32_t>& tile_min,
    std::pair<std::uint32_t, std::uint32_t>& tile_max) -> bool
  {
    tile_min = { 0U, 0U };
    tile_max = { 0U, 0U };
    if (base_tile_resolution == 0U) {
      return false;
    }

    const auto half_extent = glm::max(
      glm::vec2(full_rect_center_half_extent.z, full_rect_center_half_extent.w),
      glm::vec2(1.0e-5F, 1.0e-5F));
    const auto full_min = glm::vec2(full_rect_center_half_extent.x,
                            full_rect_center_half_extent.y)
      - half_extent;
    const auto full_max = glm::vec2(full_rect_center_half_extent.x,
                            full_rect_center_half_extent.y)
      + half_extent;
    const auto sphere_min = center_xy - glm::vec2(radius);
    const auto sphere_max = center_xy + glm::vec2(radius);
    if (sphere_max.x < full_min.x || sphere_min.x > full_max.x
      || sphere_max.y < full_min.y || sphere_min.y > full_max.y) {
      return false;
    }

    const auto normalized_min
      = glm::clamp(((sphere_min
                      - glm::vec2(full_rect_center_half_extent.x,
                        full_rect_center_half_extent.y))
                     / half_extent)
            * 0.5F
          + 0.5F,
        glm::vec2(0.0F), glm::vec2(1.0F));
    const auto normalized_max
      = glm::clamp(((sphere_max
                      - glm::vec2(full_rect_center_half_extent.x,
                        full_rect_center_half_extent.y))
                     / half_extent)
            * 0.5F
          + 0.5F,
        glm::vec2(0.0F), glm::vec2(1.0F));
    const auto ordered_min = glm::min(normalized_min, normalized_max);
    const auto ordered_max = glm::max(normalized_min, normalized_max);
    const auto max_tile = static_cast<float>(base_tile_resolution - 1U);
    tile_min = {
      std::min(static_cast<std::uint32_t>(std::floor(
                 ordered_min.x * static_cast<float>(base_tile_resolution))),
        base_tile_resolution - 1U),
      std::min(static_cast<std::uint32_t>(std::floor(
                 ordered_min.y * static_cast<float>(base_tile_resolution))),
        base_tile_resolution - 1U),
    };
    tile_max = {
      std::min(
        static_cast<std::uint32_t>(std::floor(std::max(
          ordered_max.x * static_cast<float>(base_tile_resolution) - 1.0e-5F,
          0.0F))),
        static_cast<std::uint32_t>(max_tile)),
      std::min(
        static_cast<std::uint32_t>(std::floor(std::max(
          ordered_max.y * static_cast<float>(base_tile_resolution) - 1.0e-5F,
          0.0F))),
        static_cast<std::uint32_t>(max_tile)),
    };
    return true;
  }

  [[nodiscard]] static auto BaseMaskOverlaps(
    const ConventionalShadowReceiverMaskSummary& summary,
    const std::span<const std::uint32_t> base_mask,
    const std::pair<std::uint32_t, std::uint32_t>& tile_min,
    const std::pair<std::uint32_t, std::uint32_t>& tile_max) -> bool
  {
    for (std::uint32_t tile_y = tile_min.second; tile_y <= tile_max.second;
      ++tile_y) {
      for (std::uint32_t tile_x = tile_min.first; tile_x <= tile_max.first;
        ++tile_x) {
        const auto flat_index
          = static_cast<std::size_t>(tile_y) * summary.base_tile_resolution
          + tile_x;
        if (base_mask[flat_index] != 0U) {
          return true;
        }
      }
    }
    return false;
  }

  [[nodiscard]] static auto HierarchyMaskOverlaps(
    const ConventionalShadowReceiverMaskSummary& summary,
    const std::span<const std::uint32_t> hierarchy_mask,
    const std::pair<std::uint32_t, std::uint32_t>& tile_min,
    const std::pair<std::uint32_t, std::uint32_t>& tile_max) -> bool
  {
    if (summary.hierarchy_reduction == 0U
      || summary.hierarchy_tile_resolution == 0U) {
      return false;
    }
    const auto hierarchy_min = std::pair {
      tile_min.first / summary.hierarchy_reduction,
      tile_min.second / summary.hierarchy_reduction,
    };
    const auto hierarchy_max = std::pair {
      tile_max.first / summary.hierarchy_reduction,
      tile_max.second / summary.hierarchy_reduction,
    };
    for (std::uint32_t tile_y = hierarchy_min.second;
      tile_y <= hierarchy_max.second; ++tile_y) {
      for (std::uint32_t tile_x = hierarchy_min.first;
        tile_x <= hierarchy_max.first; ++tile_x) {
        const auto flat_index
          = static_cast<std::size_t>(tile_y) * summary.hierarchy_tile_resolution
          + tile_x;
        if (hierarchy_mask[flat_index] != 0U) {
          return true;
        }
      }
    }
    return false;
  }

  [[nodiscard]] static auto CpuAcceptsSphere(
    const ConventionalShadowReceiverAnalysisJob& job,
    const ConventionalShadowReceiverMaskSummary& summary,
    const std::span<const std::uint32_t> base_mask,
    const std::span<const std::uint32_t> hierarchy_mask,
    const glm::vec4& sphere_ws) -> bool
  {
    if (sphere_ws.w <= 0.0F || summary.sample_count == 0U
      || (summary.flags
           & oxygen::renderer::kConventionalShadowReceiverMaskFlagValid)
        == 0U
      || (summary.flags
           & oxygen::renderer::
             kConventionalShadowReceiverMaskFlagHierarchyBuilt)
        == 0U) {
      return false;
    }

    const auto sphere_center_ls = glm::vec3(job.light_rotation_matrix
      * glm::vec4(sphere_ws.x, sphere_ws.y, sphere_ws.z, 1.0F));
    const auto radius = sphere_ws.w;

    const auto receiver_min
      = glm::vec2(summary.raw_xy_min_max.x, summary.raw_xy_min_max.y)
      - glm::vec2(summary.raw_depth_and_dilation.z);
    const auto receiver_max
      = glm::vec2(summary.raw_xy_min_max.z, summary.raw_xy_min_max.w)
      + glm::vec2(summary.raw_depth_and_dilation.z);
    if (sphere_center_ls.x + radius < receiver_min.x
      || sphere_center_ls.x - radius > receiver_max.x
      || sphere_center_ls.y + radius < receiver_min.y
      || sphere_center_ls.y - radius > receiver_max.y) {
      return false;
    }

    const auto receiver_min_z
      = summary.raw_depth_and_dilation.x - summary.raw_depth_and_dilation.w;
    if (sphere_center_ls.z + radius < receiver_min_z) {
      return false;
    }

    auto tile_min = std::pair { 0U, 0U };
    auto tile_max = std::pair { 0U, 0U };
    if (!ComputeSphereTileBounds(
          glm::vec2(sphere_center_ls.x, sphere_center_ls.y), radius,
          summary.full_rect_center_half_extent, summary.base_tile_resolution,
          tile_min, tile_max)) {
      return false;
    }

    return HierarchyMaskOverlaps(summary, hierarchy_mask, tile_min, tile_max)
      && BaseMaskOverlaps(summary, base_mask, tile_min, tile_max);
  }

  [[nodiscard]] static auto FindAcceptedWorldSphere(
    const ConventionalShadowReceiverAnalysisJob& job,
    const ConventionalShadowReceiverMaskSummary& summary,
    const std::span<const std::uint32_t> base_mask,
    const std::span<const std::uint32_t> hierarchy_mask) -> glm::vec4
  {
    CHECK_F(summary.base_tile_resolution > 0U,
      "receiver-mask summary has no base-tile resolution");
    CHECK_F(base_mask.size()
        == static_cast<std::size_t>(summary.base_tile_resolution)
          * summary.base_tile_resolution,
      "receiver-mask base slice size does not match summary resolution");

    const auto full_min_x = summary.full_rect_center_half_extent.x
      - summary.full_rect_center_half_extent.z;
    const auto full_min_y = summary.full_rect_center_half_extent.y
      - summary.full_rect_center_half_extent.w;
    const auto tile_extent_x = (2.0F * summary.full_rect_center_half_extent.z)
      / static_cast<float>(summary.base_tile_resolution);
    const auto tile_extent_y = (2.0F * summary.full_rect_center_half_extent.w)
      / static_cast<float>(summary.base_tile_resolution);
    const auto radius_unit
      = std::max(0.01F, 0.1F * std::min(tile_extent_x, tile_extent_y));
    const auto center_z = 0.5F
      * (summary.raw_depth_and_dilation.x + summary.raw_depth_and_dilation.y);
    const auto inverse_light_rotation = glm::inverse(job.light_rotation_matrix);

    const auto try_light_space_center
      = [&](const glm::vec3& center_ls) -> std::optional<glm::vec4> {
      for (const auto radius_scale : std::array { 1.0F, 0.5F, 0.25F }) {
        const auto radius = std::max(0.01F, radius_scale * radius_unit);
        const auto center_ws
          = inverse_light_rotation * glm::vec4(center_ls, 1.0F);
        const auto sphere_ws
          = glm::vec4(center_ws.x, center_ws.y, center_ws.z, radius);
        if (CpuAcceptsSphere(
              job, summary, base_mask, hierarchy_mask, sphere_ws)) {
          return sphere_ws;
        }
      }
      return std::nullopt;
    };

    if (const auto raw_center_candidate = try_light_space_center(glm::vec3 {
          0.5F * (summary.raw_xy_min_max.x + summary.raw_xy_min_max.z),
          0.5F * (summary.raw_xy_min_max.y + summary.raw_xy_min_max.w),
          center_z,
        });
      raw_center_candidate.has_value()) {
      return *raw_center_candidate;
    }

    for (std::uint32_t tile_y = 0U; tile_y < summary.base_tile_resolution;
      ++tile_y) {
      for (std::uint32_t tile_x = 0U; tile_x < summary.base_tile_resolution;
        ++tile_x) {
        const auto flat_index
          = static_cast<std::size_t>(tile_y) * summary.base_tile_resolution
          + tile_x;
        if (base_mask[flat_index] == 0U) {
          continue;
        }

        const auto candidate = try_light_space_center(glm::vec3 {
          full_min_x + (static_cast<float>(tile_x) + 0.5F) * tile_extent_x,
          full_min_y + (static_cast<float>(tile_y) + 0.5F) * tile_extent_y,
          center_z,
        });
        if (candidate.has_value()) {
          return *candidate;
        }
      }
    }

    return glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F };
  }

  [[nodiscard]] static auto JobContainsEyeDepth(const std::size_t job_index,
    const ConventionalShadowReceiverAnalysisJob& job, const float eye_depth)
    -> bool
  {
    const auto split_begin = job.split_and_full_depth_range.x;
    const auto split_end = job.split_and_full_depth_range.y;
    return eye_depth <= split_end
      && (job_index == 0U ? eye_depth >= split_begin : eye_depth > split_begin);
  }

  [[nodiscard]] static auto DescribeCounts(
    const std::span<const std::uint32_t> counts) -> std::string
  {
    auto stream = std::ostringstream {};
    stream << "counts=";
    for (std::size_t i = 0; i < counts.size(); ++i) {
      if (i != 0U) {
        stream << ",";
      }
      stream << counts[i];
    }
    return stream.str();
  }
};

NOLINT_TEST_F(ConventionalShadowCasterCullingPassTest,
  CompactsConventionalShadowDrawsAgainstReceiverMask)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("caster-culling.scene", 64U);
  auto directional_node = CreateShadowCastingDirectionalNode(*scene);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 1U };
  const auto resolved_view = MakeResolvedView();
  const auto view_constants
    = MakeViewConstants(resolved_view, kFrameSlot, kFrameSequence);

  auto opaque_shadow_mask = MakeShadowCasterMask(false);
  auto masked_shadow_mask = MakeShadowCasterMask(true);

  SyntheticSceneBuilder synthetic_builder(
    Backend(), *renderer, "caster-culling.synthetic");
  synthetic_builder.AddTriangle(MakeTestVertex(-0.2F, -0.2F, -1.0F),
    MakeTestVertex(0.2F, -0.2F, -1.0F), MakeTestVertex(0.0F, 0.2F, -1.0F),
    opaque_shadow_mask);
  synthetic_builder.AddTriangle(MakeTestVertex(-0.3F, -0.1F, -1.5F),
    MakeTestVertex(0.3F, -0.1F, -1.5F), MakeTestVertex(0.0F, 0.4F, -1.5F),
    opaque_shadow_mask);
  synthetic_builder.AddTriangle(MakeTestVertex(-0.1F, -0.3F, -2.0F),
    MakeTestVertex(0.1F, -0.3F, -2.0F), MakeTestVertex(0.0F, 0.1F, -2.0F),
    masked_shadow_mask);
  auto synthetic_scene = synthetic_builder.Build(
    kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  auto draw_bounding_spheres = std::vector<glm::vec4>(3U);
  auto conventional_draw_records
    = std::vector<oxygen::renderer::ConventionalShadowDrawRecord> {};

  auto prepared_frame = synthetic_scene.prepared_frame;
  prepared_frame.draw_bounding_spheres = std::span(draw_bounding_spheres);

  auto depth_texture = CreateDepthTexture("caster-culling.depth");
  ASSERT_NE(depth_texture, nullptr);
  SeedDepthPatch(depth_texture, resolved_view, "caster-culling.depth");

  auto offscreen = renderer->BeginOffscreenFrame({ .frame_slot = kFrameSlot,
    .frame_sequence = kFrameSequence,
    .scene = observer_ptr<Scene> { scene.get() } });
  offscreen.SetCurrentView(
    kTestViewId, resolved_view, prepared_frame, view_constants);
  auto& render_context = offscreen.GetRenderContext();

  auto depth_config = std::make_shared<DepthPrePass::Config>();
  depth_config->depth_texture = depth_texture;
  depth_config->debug_name = "caster-culling.depth-prepass";
  auto depth_pass = DepthPrePass(depth_config);
  render_context.RegisterPass(&depth_pass);

  auto light_manager = renderer->GetLightManager();
  ASSERT_NE(light_manager.get(), nullptr);
  light_manager->CollectFromNode(
    directional_node.GetHandle(), directional_impl->get());

  auto shadow_manager = renderer->GetShadowManager();
  ASSERT_NE(shadow_manager.get(), nullptr);
  shadow_manager->ReserveFrameResources(1U, *light_manager);

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, -5.0F, 10.0F } };
  shadow_manager->PublishForView(kTestViewId, view_constants, *light_manager,
    observer_ptr<Scene> { scene.get() }, static_cast<float>(kWidth), {},
    shadow_caster_bounds);
  const auto* plan = shadow_manager->TryGetReceiverAnalysisPlan(kTestViewId);
  ASSERT_NE(plan, nullptr);
  ASSERT_FALSE(plan->jobs.empty());

  auto hzb_pass = ScreenHzbBuildPass(observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ScreenHzbBuildPassConfig>(
      ScreenHzbBuildPassConfig { .debug_name = "caster-culling.hzb" }));
  auto receiver_analysis_pass = ConventionalShadowReceiverAnalysisPass(
    observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowReceiverAnalysisPassConfig>(
      ConventionalShadowReceiverAnalysisPassConfig {
        .debug_name = "caster-culling.analysis",
      }));
  auto receiver_mask_pass = ConventionalShadowReceiverMaskPass(
    observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowReceiverMaskPassConfig>(
      ConventionalShadowReceiverMaskPassConfig {
        .debug_name = "caster-culling.mask",
      }));
  auto caster_culling_pass = ConventionalShadowCasterCullingPass(
    observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowCasterCullingPassConfig>(
      ConventionalShadowCasterCullingPassConfig {
        .debug_name = "caster-culling",
      }));

  {
    auto recorder = AcquireRecorder("caster-culling.prepare-mask");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);

    oxygen::co::testing::TestEventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      co_await depth_pass.PrepareResources(render_context, *recorder);
      co_return;
    });

    RunPass(hzb_pass, render_context, *recorder);
    render_context.RegisterPass<ScreenHzbBuildPass>(&hzb_pass);
    RunPass(receiver_analysis_pass, render_context, *recorder);
    render_context.RegisterPass<ConventionalShadowReceiverAnalysisPass>(
      &receiver_analysis_pass);
    RunPass(receiver_mask_pass, render_context, *recorder);
    render_context.RegisterPass<ConventionalShadowReceiverMaskPass>(
      &receiver_mask_pass);
  }
  WaitForQueueIdle();

  const auto receiver_mask_output
    = receiver_mask_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(receiver_mask_output.available);
  auto readback_manager = Backend().GetReadbackManager();
  ASSERT_NE(readback_manager, nullptr);
  const auto summary_bytes
    = readback_manager->ReadBufferNow(*receiver_mask_output.summary_buffer);
  ASSERT_TRUE(summary_bytes.has_value());
  const auto base_bytes
    = readback_manager->ReadBufferNow(*receiver_mask_output.base_mask_buffer);
  ASSERT_TRUE(base_bytes.has_value());
  const auto hierarchy_bytes = readback_manager->ReadBufferNow(
    *receiver_mask_output.hierarchy_mask_buffer);
  ASSERT_TRUE(hierarchy_bytes.has_value());

  auto summaries = std::vector<ConventionalShadowReceiverMaskSummary>(
    receiver_mask_output.job_count);
  std::memcpy(summaries.data(), summary_bytes->data(), summary_bytes->size());
  auto base_mask
    = std::vector<std::uint32_t>(base_bytes->size() / sizeof(std::uint32_t));
  std::memcpy(base_mask.data(), base_bytes->data(), base_bytes->size());
  auto hierarchy_mask = std::vector<std::uint32_t>(
    hierarchy_bytes->size() / sizeof(std::uint32_t));
  std::memcpy(
    hierarchy_mask.data(), hierarchy_bytes->data(), hierarchy_bytes->size());
  const auto base_tiles_per_job
    = static_cast<std::size_t>(receiver_mask_output.base_tile_resolution
      * receiver_mask_output.base_tile_resolution);
  const auto hierarchy_tiles_per_job
    = static_cast<std::size_t>(receiver_mask_output.hierarchy_tile_resolution
      * receiver_mask_output.hierarchy_tile_resolution);
  ASSERT_EQ(base_mask.size(),
    static_cast<std::size_t>(receiver_mask_output.job_count)
      * base_tiles_per_job);
  ASSERT_EQ(hierarchy_mask.size(),
    static_cast<std::size_t>(receiver_mask_output.job_count)
      * hierarchy_tiles_per_job);

  auto sampled_jobs = std::vector<std::uint32_t> {};
  for (std::uint32_t i = 0U; i < summaries.size(); ++i) {
    if (summaries[i].sample_count > 0U) {
      sampled_jobs.push_back(i);
    }
  }
  ASSERT_FALSE(sampled_jobs.empty());

  auto primary_job_index = std::optional<std::uint32_t> {};
  auto secondary_job_index = std::optional<std::uint32_t> {};
  for (const auto job_index : sampled_jobs) {
    if (!primary_job_index.has_value()
      && JobContainsEyeDepth(job_index, plan->jobs[job_index], 1.25F)) {
      primary_job_index = job_index;
    }
    if (!secondary_job_index.has_value()
      && JobContainsEyeDepth(job_index, plan->jobs[job_index], 3.50F)) {
      secondary_job_index = job_index;
    }
  }
  ASSERT_TRUE(primary_job_index.has_value());
  ASSERT_TRUE(secondary_job_index.has_value());
  const auto primary_base_mask = std::span<const std::uint32_t>(base_mask.data()
      + static_cast<std::size_t>(*primary_job_index) * base_tiles_per_job,
    base_tiles_per_job);
  const auto secondary_base_mask
    = std::span<const std::uint32_t>(base_mask.data()
        + static_cast<std::size_t>(*secondary_job_index) * base_tiles_per_job,
      base_tiles_per_job);
  const auto primary_hierarchy_mask = std::span<const std::uint32_t>(
    hierarchy_mask.data()
      + static_cast<std::size_t>(*primary_job_index) * hierarchy_tiles_per_job,
    hierarchy_tiles_per_job);
  const auto secondary_hierarchy_mask
    = std::span<const std::uint32_t>(hierarchy_mask.data()
        + static_cast<std::size_t>(*secondary_job_index)
          * hierarchy_tiles_per_job,
      hierarchy_tiles_per_job);
  draw_bounding_spheres[0]
    = FindAcceptedWorldSphere(plan->jobs[*primary_job_index],
      summaries[*primary_job_index], primary_base_mask, primary_hierarchy_mask);
  draw_bounding_spheres[1] = BuildRejectedWorldSphere(
    plan->jobs[*primary_job_index], summaries[*primary_job_index]);
  draw_bounding_spheres[2] = FindAcceptedWorldSphere(
    plan->jobs[*secondary_job_index], summaries[*secondary_job_index],
    secondary_base_mask, secondary_hierarchy_mask);
  ASSERT_GT(draw_bounding_spheres[0].w, 0.0F);
  ASSERT_GT(draw_bounding_spheres[2].w, 0.0F);
  ASSERT_TRUE(CpuAcceptsSphere(plan->jobs[*primary_job_index],
    summaries[*primary_job_index], primary_base_mask, primary_hierarchy_mask,
    draw_bounding_spheres[0]));
  ASSERT_FALSE(CpuAcceptsSphere(plan->jobs[*primary_job_index],
    summaries[*primary_job_index], primary_base_mask, primary_hierarchy_mask,
    draw_bounding_spheres[1]));
  ASSERT_TRUE(CpuAcceptsSphere(plan->jobs[*secondary_job_index],
    summaries[*secondary_job_index], secondary_base_mask,
    secondary_hierarchy_mask, draw_bounding_spheres[2]));

  BuildConventionalShadowDrawRecords(prepared_frame, conventional_draw_records);
  ASSERT_EQ(conventional_draw_records.size(), 3U);

  const auto uploaded_draw_records = UploadStructuredSrvBuffer(
    std::span<const oxygen::renderer::ConventionalShadowDrawRecord>(
      conventional_draw_records.data(), conventional_draw_records.size()),
    "caster-culling.draw-records");
  prepared_frame.conventional_shadow_draw_records
    = std::span(conventional_draw_records);
  prepared_frame.bindless_conventional_shadow_draw_records_slot
    = uploaded_draw_records.slot;
  offscreen.SetCurrentView(
    kTestViewId, resolved_view, prepared_frame, view_constants);

  {
    auto recorder = AcquireRecorder("caster-culling.run");
    ASSERT_NE(recorder, nullptr);
    RunPass(caster_culling_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto output = caster_culling_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_EQ(output.job_count, receiver_mask_output.job_count);

  const auto partitions
    = caster_culling_pass.GetIndirectPartitionsForInspection(kTestViewId);
  ASSERT_EQ(partitions.size(), 2U);

  const auto opaque_partition = std::find_if(
    partitions.begin(), partitions.end(), [](const auto& partition) {
      return partition.pass_mask.IsSet(PassMaskBit::kOpaque);
    });
  const auto masked_partition = std::find_if(
    partitions.begin(), partitions.end(), [](const auto& partition) {
      return partition.pass_mask.IsSet(PassMaskBit::kMasked);
    });
  ASSERT_NE(opaque_partition, partitions.end());
  ASSERT_NE(masked_partition, partitions.end());

  auto emitted_draw_indices = std::set<std::uint32_t> {};
  auto read_counts =
    [&](const ConventionalShadowCasterCullingPass::IndirectPartitionInspection&
        partition) -> std::vector<std::uint32_t> {
    const auto count_bytes
      = readback_manager->ReadBufferNow(*partition.count_buffer);
    EXPECT_TRUE(count_bytes.has_value());
    if (!count_bytes.has_value()) {
      return {};
    }
    auto counts = std::vector<std::uint32_t>(output.job_count);
    std::memcpy(counts.data(), count_bytes->data(), count_bytes->size());

    const auto command_bytes
      = readback_manager->ReadBufferNow(*partition.command_buffer);
    EXPECT_TRUE(command_bytes.has_value());
    if (!command_bytes.has_value()) {
      return {};
    }
    auto commands = std::vector<ConventionalShadowIndirectDrawCommand>(
      static_cast<std::size_t>(output.job_count)
      * partition.max_commands_per_job);
    std::memcpy(commands.data(), command_bytes->data(), command_bytes->size());

    for (std::uint32_t job_index = 0U; job_index < output.job_count;
      ++job_index) {
      const auto command_base
        = static_cast<std::size_t>(job_index) * partition.max_commands_per_job;
      for (std::uint32_t slot = 0U; slot < counts[job_index]; ++slot) {
        emitted_draw_indices.insert(commands[command_base + slot].draw_index);
      }
    }
    return counts;
  };

  const auto opaque_counts = read_counts(*opaque_partition);
  const auto masked_counts = read_counts(*masked_partition);
  ASSERT_EQ(opaque_counts.size(), output.job_count);
  ASSERT_EQ(masked_counts.size(), output.job_count);

  EXPECT_EQ(opaque_counts[*primary_job_index], 1U)
    << DescribeCounts(opaque_counts);
  EXPECT_LT(
    opaque_counts[*primary_job_index], opaque_partition->draw_record_count)
    << DescribeCounts(opaque_counts);
  EXPECT_EQ(masked_counts[*secondary_job_index], 1U)
    << DescribeCounts(masked_counts);

  EXPECT_TRUE(emitted_draw_indices.contains(0U));
  EXPECT_TRUE(emitted_draw_indices.contains(2U));
  EXPECT_FALSE(emitted_draw_indices.contains(1U));
  EXPECT_LT(std::accumulate(opaque_counts.begin(), opaque_counts.end(), 0U),
    opaque_partition->draw_record_count * output.job_count);
}

} // namespace
