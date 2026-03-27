//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/DirectionalShadowCandidate.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "VirtualShadowStageGpuHarness.h"

namespace oxygen::renderer::internal {
inline auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace oxygen::renderer::vsm::testing {

struct TwoBoxShadowSceneData {
  std::shared_ptr<oxygen::scene::Scene> scene {};
  oxygen::scene::SceneNode sun_node {};
  oxygen::scene::SceneNode spot_node {};
  oxygen::scene::SceneNode floor_node {};
  oxygen::scene::SceneNode tall_box_node {};
  oxygen::scene::SceneNode short_box_node {};
  StageShaderVisibleBuffer floor_vertex_buffer {};
  StageShaderVisibleBuffer floor_index_buffer {};
  StageShaderVisibleBuffer cube_vertex_buffer {};
  StageShaderVisibleBuffer cube_index_buffer {};
  StageShaderVisibleBuffer draw_bounds_buffer {};
  std::array<glm::mat4, 3> world_matrices {};
  std::array<oxygen::engine::DrawMetadata, 3> draw_records {};
  std::array<oxygen::engine::sceneprep::RenderItemData, 3> rendered_items {};
  std::array<glm::vec4, 3> draw_bounds {};
  std::array<glm::vec4, 2> shadow_caster_bounds {};
  std::array<glm::vec4, 1> receiver_bounds {};
  oxygen::engine::PassMask receiver_mask {};
  oxygen::engine::PassMask shadow_caster_mask {};
  glm::vec3 sun_direction_ws {};
  glm::vec3 spot_position_ws {};
  glm::vec3 spot_direction_ws {};
  float spot_range { 0.0F };
  float spot_inner_cone_radians { 0.0F };
  float spot_outer_cone_radians { 0.0F };
};

struct TwoBoxLiveFrameResult {
  oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame virtual_frame {};
  const oxygen::renderer::vsm::VsmShadowRenderer::PreparedViewState*
    prepared_view { nullptr };
  const oxygen::renderer::vsm::VsmExtractedCacheFrame* extracted_frame {
    nullptr
  };
};

class VsmLiveSceneHarness : public VsmStageGpuHarness {
protected:
  using EventLoop = oxygen::co::testing::TestEventLoop;

  [[nodiscard]] static auto MakeLookAtResolvedView(const glm::vec3& eye,
    const glm::vec3& target, const std::uint32_t width,
    const std::uint32_t height, const float fov_y_radians = glm::radians(60.0F))
    -> oxygen::ResolvedView
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
      .view_matrix = glm::lookAtRH(eye, target, glm::vec3 { 0.0F, 1.0F, 0.0F }),
      .proj_matrix = glm::perspectiveRH_ZO(fov_y_radians,
        static_cast<float>(width) / static_cast<float>(height), 0.1F, 100.0F),
      .camera_position = eye,
      .depth_range = oxygen::NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto RotationFromForwardTo(const glm::vec3& direction)
    -> glm::quat
  {
    const auto start = glm::normalize(glm::vec3 { 0.0F, 0.0F, -1.0F });
    const auto dest = glm::normalize(direction);
    const auto cos_theta = glm::dot(start, dest);

    if (cos_theta < -0.9999F) {
      auto axis = glm::cross(glm::vec3 { 0.0F, 1.0F, 0.0F }, start);
      if (glm::dot(axis, axis) < 1.0e-6F) {
        axis = glm::cross(glm::vec3 { 1.0F, 0.0F, 0.0F }, start);
      }
      return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }

    const auto axis = glm::cross(start, dest);
    const auto s = std::sqrt((1.0F + cos_theta) * 2.0F);
    const auto inv_s = 1.0F / s;
    return glm::normalize(glm::quat {
      s * 0.5F,
      axis.x * inv_s,
      axis.y * inv_s,
      axis.z * inv_s,
    });
  }

  [[nodiscard]] static auto MakeViewConstants(
    const oxygen::ResolvedView& resolved_view,
    const oxygen::frame::SequenceNumber sequence,
    const oxygen::frame::Slot slot,
    const oxygen::ShaderVisibleIndex view_frame_slot
    = oxygen::ShaderVisibleIndex { 1U }) -> oxygen::engine::ViewConstants
  {
    auto view_constants = oxygen::engine::ViewConstants {};
    view_constants.SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition())
      .SetFrameSlot(slot, oxygen::engine::ViewConstants::kRenderer)
      .SetFrameSequenceNumber(
        sequence, oxygen::engine::ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        oxygen::engine::BindlessViewFrameBindingsSlot { view_frame_slot },
        oxygen::engine::ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, oxygen::engine::ViewConstants::kRenderer);
    return view_constants;
  }

  static auto UpdateTransforms(
    oxygen::scene::Scene& scene, oxygen::scene::SceneNode& node) -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(scene);
  }

  static auto CollectSceneLights(oxygen::renderer::LightManager& lights,
    TwoBoxShadowSceneData& scene_data) -> void
  {
    if (scene_data.sun_node.IsValid()) {
      auto sun_impl = scene_data.sun_node.GetImpl();
      if (!sun_impl.has_value()) {
        ADD_FAILURE() << "directional light node has no implementation";
      } else {
        lights.CollectFromNode(
          scene_data.sun_node.GetHandle(), sun_impl->get());
      }
    }

    if (scene_data.spot_node.IsValid()) {
      auto spot_impl = scene_data.spot_node.GetImpl();
      if (!spot_impl.has_value()) {
        ADD_FAILURE() << "spot light node has no implementation";
      } else {
        lights.CollectFromNode(
          scene_data.spot_node.GetHandle(), spot_impl->get());
      }
    }
  }

  auto AttachSpotLightToTwoBoxScene(TwoBoxShadowSceneData& scene_data,
    const glm::vec3& spot_position_ws, const glm::vec3& spot_target_ws,
    const float range, const float inner_cone_radians = glm::radians(22.0F),
    const float outer_cone_radians = glm::radians(32.0F)) -> void
  {
    CHECK_NOTNULL_F(scene_data.scene.get(),
      "Two-box scene must exist before adding a spot light");

    const auto visible_flags
      = oxygen::scene::SceneNode::Flags {}
          .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
          .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    scene_data.spot_node = scene_data.scene->CreateNode("spot", visible_flags);
    CHECK_F(scene_data.spot_node.IsValid(), "Failed to create spot node");
    CHECK_F(scene_data.spot_node.AttachLight(
              std::make_unique<oxygen::scene::SpotLight>()),
      "Failed to attach spot light");

    auto spot_impl = scene_data.spot_node.GetImpl();
    CHECK_F(spot_impl.has_value(), "Spot node has no implementation");
    auto& spot_light
      = spot_impl->get().GetComponent<oxygen::scene::SpotLight>();
    spot_light.Common().casts_shadows = true;
    spot_light.SetRange(range);
    spot_light.SetInnerConeAngleRadians(inner_cone_radians);
    spot_light.SetOuterConeAngleRadians(outer_cone_radians);

    CHECK_F(
      scene_data.spot_node.GetTransform().SetLocalPosition(spot_position_ws),
      "Failed to position spot node");
    CHECK_F(scene_data.spot_node.GetTransform().SetLocalRotation(
              RotationFromForwardTo(spot_target_ws - spot_position_ws)),
      "Failed to rotate spot node");
    UpdateTransforms(*scene_data.scene, scene_data.spot_node);

    scene_data.spot_position_ws = spot_position_ws;
    scene_data.spot_direction_ws
      = glm::normalize(spot_target_ws - spot_position_ws);
    scene_data.spot_range = range;
    scene_data.spot_inner_cone_radians = inner_cone_radians;
    scene_data.spot_outer_cone_radians = outer_cone_radians;
  }

  auto ExecutePreparedViewShell(
    oxygen::renderer::vsm::VsmShadowRenderer& renderer,
    const oxygen::engine::RenderContext& render_context,
    const oxygen::observer_ptr<const oxygen::graphics::Texture>
      scene_depth_texture) -> void
  {
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      auto recorder = Backend().AcquireCommandRecorder(
        Backend().QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
        "early-stage-live-shell");
      EXPECT_NE(recorder.get(), nullptr);
      if (recorder == nullptr) {
        co_return;
      }
      co_await renderer.ExecutePreparedViewShell(
        render_context, *recorder, scene_depth_texture);
    });
    const auto completed_value = WaitForQueueIdle();
    EXPECT_NE(completed_value.get(), std::numeric_limits<std::uint64_t>::max());
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
      handle.IsValid(), "Failed to allocate raw SRV for `{}`", debug_name);

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
    CHECK_F(view->IsValid(), "Failed to register raw SRV for `{}`", debug_name);

    return StageShaderVisibleBuffer {
      .buffer = std::move(buffer),
      .slot = slot,
    };
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
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = oxygen::graphics::ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  auto CreateTwoBoxShadowScene(const glm::vec3& sun_direction_ws,
    const std::uint32_t cascade_count = 4U) -> TwoBoxShadowSceneData
  {
    using oxygen::engine::DrawMetadata;
    using oxygen::engine::DrawPrimitiveFlagBits;
    using oxygen::engine::PassMask;
    using oxygen::engine::PassMaskBit;
    using oxygen::engine::sceneprep::RenderItemData;
    using oxygen::engine::sceneprep::TransformHandle;
    using oxygen::scene::DirectionalLight;
    using oxygen::scene::SceneNode;
    using oxygen::scene::SceneNodeFlags;

    auto scene_data = TwoBoxShadowSceneData {};
    scene_data.scene
      = std::make_shared<oxygen::scene::Scene>("early-stage-two-box-scene", 32);

    const auto visible_flags
      = SceneNode::Flags {}
          .SetFlag(SceneNodeFlags::kVisible,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
          .SetFlag(SceneNodeFlags::kCastsShadows,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    scene_data.sun_node = scene_data.scene->CreateNode("sun", visible_flags);
    scene_data.floor_node
      = scene_data.scene->CreateNode("floor", visible_flags);
    scene_data.tall_box_node
      = scene_data.scene->CreateNode("tall-box", visible_flags);
    scene_data.short_box_node
      = scene_data.scene->CreateNode("short-box", visible_flags);
    CHECK_F(scene_data.sun_node.IsValid(), "Failed to create sun node");
    CHECK_F(scene_data.floor_node.IsValid(), "Failed to create floor node");
    CHECK_F(
      scene_data.tall_box_node.IsValid(), "Failed to create tall box node");
    CHECK_F(
      scene_data.short_box_node.IsValid(), "Failed to create short box node");
    CHECK_F(
      scene_data.sun_node.AttachLight(std::make_unique<DirectionalLight>()),
      "Failed to attach directional light");

    auto sun_impl = scene_data.sun_node.GetImpl();
    CHECK_F(sun_impl.has_value(), "Sun node has no implementation");
    auto& sun_light = sun_impl->get().GetComponent<DirectionalLight>();
    sun_light.Common().casts_shadows = true;
    sun_light.SetIsSunLight(true);
    sun_light.CascadedShadows().cascade_count = cascade_count;
    CHECK_F(scene_data.sun_node.GetTransform().SetLocalRotation(
              RotationFromForwardTo(sun_direction_ws)),
      "Failed to rotate sun node");
    UpdateTransforms(*scene_data.scene, scene_data.sun_node);
    scene_data.sun_direction_ws = sun_direction_ws;

    const auto make_vertex = [](const glm::vec3& position) {
      return StageMeshVertex {
        .position = position,
        .normal = { 0.0F, 1.0F, 0.0F },
        .texcoord = { 0.0F, 0.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 0.0F, 1.0F },
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
      };
    };

    const std::array<StageMeshVertex, 4> floor_vertices {
      make_vertex(glm::vec3 { -4.5F, 0.0F, -4.5F }),
      make_vertex(glm::vec3 { 4.5F, 0.0F, -4.5F }),
      make_vertex(glm::vec3 { 4.5F, 0.0F, 4.5F }),
      make_vertex(glm::vec3 { -4.5F, 0.0F, 4.5F }),
    };
    constexpr std::array<std::uint32_t, 6> kFloorIndices {
      0U,
      2U,
      1U,
      0U,
      3U,
      2U,
    };

    const std::array<StageMeshVertex, 8> cube_vertices {
      make_vertex(glm::vec3 { -0.5F, 0.0F, -0.5F }),
      make_vertex(glm::vec3 { 0.5F, 0.0F, -0.5F }),
      make_vertex(glm::vec3 { 0.5F, 1.0F, -0.5F }),
      make_vertex(glm::vec3 { -0.5F, 1.0F, -0.5F }),
      make_vertex(glm::vec3 { -0.5F, 0.0F, 0.5F }),
      make_vertex(glm::vec3 { 0.5F, 0.0F, 0.5F }),
      make_vertex(glm::vec3 { 0.5F, 1.0F, 0.5F }),
      make_vertex(glm::vec3 { -0.5F, 1.0F, 0.5F }),
    };
    constexpr std::array<std::uint32_t, 36> kCubeIndices {
      0U,
      1U,
      2U,
      0U,
      2U,
      3U,
      4U,
      6U,
      5U,
      4U,
      7U,
      6U,
      0U,
      4U,
      5U,
      0U,
      5U,
      1U,
      1U,
      5U,
      6U,
      1U,
      6U,
      2U,
      2U,
      6U,
      7U,
      2U,
      7U,
      3U,
      3U,
      7U,
      4U,
      3U,
      4U,
      0U,
    };

    scene_data.floor_vertex_buffer = CreateStructuredSrvBuffer<StageMeshVertex>(
      floor_vertices, "early-stage-two-box.floor.vertices");
    scene_data.floor_index_buffer = CreateUIntIndexBuffer(
      kFloorIndices, "early-stage-two-box.floor.indices");
    scene_data.cube_vertex_buffer = CreateStructuredSrvBuffer<StageMeshVertex>(
      cube_vertices, "early-stage-two-box.cube.vertices");
    scene_data.cube_index_buffer
      = CreateUIntIndexBuffer(kCubeIndices, "early-stage-two-box.cube.indices");

    scene_data.world_matrices = {
      glm::mat4 { 1.0F },
      glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 0.95F, 0.0F, -0.25F })
        * glm::scale(glm::mat4 { 1.0F }, glm::vec3 { 0.8F, 3.2F, 0.8F }),
      glm::translate(glm::mat4 { 1.0F }, glm::vec3 { -0.55F, 0.0F, 0.65F })
        * glm::scale(glm::mat4 { 1.0F }, glm::vec3 { 0.8F, 1.1F, 0.8F }),
    };

    scene_data.receiver_mask = PassMask {};
    scene_data.receiver_mask.Set(PassMaskBit::kOpaque);
    scene_data.receiver_mask.Set(PassMaskBit::kMainViewVisible);
    scene_data.receiver_mask.Set(PassMaskBit::kDoubleSided);

    scene_data.shadow_caster_mask = PassMask {};
    scene_data.shadow_caster_mask.Set(PassMaskBit::kOpaque);
    scene_data.shadow_caster_mask.Set(PassMaskBit::kMainViewVisible);
    scene_data.shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

    constexpr auto kMainViewVisiblePrimitiveFlag
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);

    scene_data.draw_records = {
      DrawMetadata {
        .vertex_buffer_index = scene_data.floor_vertex_buffer.slot,
        .index_buffer_index = scene_data.floor_index_buffer.slot,
        .first_index = 0U,
        .base_vertex = 0,
        .is_indexed = 1U,
        .instance_count = 1U,
        .index_count = static_cast<std::uint32_t>(kFloorIndices.size()),
        .vertex_count = 0U,
        .material_handle = 0U,
        .transform_index = 0U,
        .instance_metadata_buffer_index = 0U,
        .instance_metadata_offset = 0U,
        .flags = scene_data.receiver_mask,
        .transform_generation = 41U,
        .submesh_index = 0U,
        .primitive_flags = kMainViewVisiblePrimitiveFlag,
      },
      DrawMetadata {
        .vertex_buffer_index = scene_data.cube_vertex_buffer.slot,
        .index_buffer_index = scene_data.cube_index_buffer.slot,
        .first_index = 0U,
        .base_vertex = 0,
        .is_indexed = 1U,
        .instance_count = 1U,
        .index_count = static_cast<std::uint32_t>(kCubeIndices.size()),
        .vertex_count = 0U,
        .material_handle = 0U,
        .transform_index = 1U,
        .instance_metadata_buffer_index = 0U,
        .instance_metadata_offset = 0U,
        .flags = scene_data.shadow_caster_mask,
        .transform_generation = 42U,
        .submesh_index = 0U,
        .primitive_flags = kMainViewVisiblePrimitiveFlag,
      },
      DrawMetadata {
        .vertex_buffer_index = scene_data.cube_vertex_buffer.slot,
        .index_buffer_index = scene_data.cube_index_buffer.slot,
        .first_index = 0U,
        .base_vertex = 0,
        .is_indexed = 1U,
        .instance_count = 1U,
        .index_count = static_cast<std::uint32_t>(kCubeIndices.size()),
        .vertex_count = 0U,
        .material_handle = 0U,
        .transform_index = 2U,
        .instance_metadata_buffer_index = 0U,
        .instance_metadata_offset = 0U,
        .flags = scene_data.shadow_caster_mask,
        .transform_generation = 43U,
        .submesh_index = 0U,
        .primitive_flags = kMainViewVisiblePrimitiveFlag,
      },
    };

    scene_data.draw_bounds = {
      glm::vec4 { 0.0F, 0.0F, 0.0F, 6.5F },
      glm::vec4 { 0.95F, 1.6F, -0.25F, 1.75F },
      glm::vec4 { -0.55F, 0.55F, 0.65F, 0.95F },
    };
    scene_data.shadow_caster_bounds = {
      scene_data.draw_bounds[1],
      scene_data.draw_bounds[2],
    };
    scene_data.receiver_bounds = {
      scene_data.draw_bounds[0],
    };

    scene_data.rendered_items = {
      RenderItemData {
        .submesh_index = 0U,
        .node_handle = scene_data.floor_node.GetHandle(),
        .world_bounding_sphere = scene_data.draw_bounds[0],
        .transform_handle = TransformHandle {
          TransformHandle::Index { 0U },
          TransformHandle::Generation { 41U },
        },
        .cast_shadows = false,
        .receive_shadows = true,
        .main_view_visible = true,
        .static_shadow_caster = false,
      },
      RenderItemData {
        .submesh_index = 0U,
        .node_handle = scene_data.tall_box_node.GetHandle(),
        .world_bounding_sphere = scene_data.draw_bounds[1],
        .transform_handle = TransformHandle {
          TransformHandle::Index { 1U },
          TransformHandle::Generation { 42U },
        },
        .cast_shadows = true,
        .receive_shadows = true,
        .main_view_visible = true,
        .static_shadow_caster = false,
      },
      RenderItemData {
        .submesh_index = 0U,
        .node_handle = scene_data.short_box_node.GetHandle(),
        .world_bounding_sphere = scene_data.draw_bounds[2],
        .transform_handle = TransformHandle {
          TransformHandle::Index { 2U },
          TransformHandle::Generation { 43U },
        },
        .cast_shadows = true,
        .receive_shadows = true,
        .main_view_visible = true,
        .static_shadow_caster = false,
      },
    };

    scene_data.draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
      scene_data.draw_bounds, "early-stage-two-box.bounds");
    return scene_data;
  }

  auto PrepareTwoBoxView(oxygen::engine::Renderer& renderer,
    TwoBoxShadowSceneData& scene_data,
    oxygen::renderer::vsm::VsmShadowRenderer& vsm_renderer,
    const oxygen::ResolvedView& resolved_view,
    const oxygen::frame::SequenceNumber sequence,
    const oxygen::frame::Slot slot, const float viewport_width,
    const std::uint64_t shadow_caster_content_hash)
    -> const oxygen::renderer::vsm::VsmShadowRenderer::PreparedViewState*
  {
    auto lights = oxygen::renderer::LightManager(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer.GetStagingProvider() },
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() });
    lights.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence, slot);
    CollectSceneLights(lights, scene_data);

    const auto has_work = vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, slot), lights,
      oxygen::observer_ptr<oxygen::scene::Scene> { scene_data.scene.get() },
      viewport_width, std::span(scene_data.rendered_items),
      std::span(scene_data.shadow_caster_bounds),
      std::span(scene_data.receiver_bounds), std::chrono::milliseconds { 16 },
      shadow_caster_content_hash);
    EXPECT_TRUE(has_work);
    return vsm_renderer.TryGetPreparedViewState(kTestViewId);
  }

  auto RunTwoBoxLiveShellFrame(oxygen::engine::Renderer& renderer,
    TwoBoxShadowSceneData& scene_data,
    oxygen::renderer::vsm::VsmShadowRenderer& vsm_renderer,
    const oxygen::ResolvedView& resolved_view, const std::uint32_t width,
    const std::uint32_t height, const oxygen::frame::SequenceNumber sequence,
    const oxygen::frame::Slot slot,
    const std::uint64_t shadow_caster_content_hash) -> TwoBoxLiveFrameResult
  {
    using oxygen::engine::DrawFrameBindings;
    using oxygen::engine::PreparedSceneFrame;
    using oxygen::engine::ViewFrameBindings;
    using oxygen::engine::internal::PerViewStructuredPublisher;
    using oxygen::engine::upload::TransientStructuredBuffer;

    auto frame_config = oxygen::engine::Renderer::OffscreenFrameConfig {
      .frame_slot = slot,
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<oxygen::scene::Scene> {
        scene_data.scene.get(),
      },
    };
    auto offscreen = renderer.BeginOffscreenFrame(frame_config);

    auto world_buffer = TransientStructuredBuffer(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer.GetStagingProvider(), sizeof(glm::mat4),
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "early-stage-two-box.worlds");
    world_buffer.OnFrameStart(sequence, slot);
    auto world_allocation = world_buffer.Allocate(
      static_cast<std::uint32_t>(scene_data.world_matrices.size()));
    CHECK_F(world_allocation.has_value(), "Failed to allocate world buffer");
    CHECK_F(
      world_allocation->IsValid(sequence), "World allocation is not valid");
    std::memcpy(world_allocation->mapped_ptr, scene_data.world_matrices.data(),
      sizeof(scene_data.world_matrices));

    auto draw_metadata_buffer = TransientStructuredBuffer(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer.GetStagingProvider(), sizeof(oxygen::engine::DrawMetadata),
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "early-stage-two-box.draws");
    draw_metadata_buffer.OnFrameStart(sequence, slot);
    auto draw_allocation = draw_metadata_buffer.Allocate(
      static_cast<std::uint32_t>(scene_data.draw_records.size()));
    CHECK_F(
      draw_allocation.has_value(), "Failed to allocate draw metadata buffer");
    CHECK_F(draw_allocation->IsValid(sequence),
      "Draw metadata allocation is not valid");
    std::memcpy(draw_allocation->mapped_ptr, scene_data.draw_records.data(),
      sizeof(scene_data.draw_records));

    auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer.GetStagingProvider(),
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "early-stage-two-box.DrawFrameBindings");
    draw_frame_publisher.OnFrameStart(sequence, slot);
    const auto draw_frame_slot = draw_frame_publisher.Publish(kTestViewId,
      DrawFrameBindings {
        .draw_metadata_slot
        = oxygen::engine::BindlessDrawMetadataSlot(draw_allocation->srv),
        .transforms_slot
        = oxygen::engine::BindlessWorldsSlot(world_allocation->srv),
      });
    CHECK_F(draw_frame_slot.IsValid(), "Draw frame slot is invalid");

    auto view_frame_publisher = PerViewStructuredPublisher<ViewFrameBindings>(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer.GetStagingProvider(),
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "early-stage-two-box.ViewFrameBindings");
    view_frame_publisher.OnFrameStart(sequence, slot);
    const auto view_frame_slot = view_frame_publisher.Publish(
      kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
    CHECK_F(view_frame_slot.IsValid(), "View frame slot is invalid");

    auto world_matrix_floats = std::array<float, 48> {};
    std::memcpy(world_matrix_floats.data(), scene_data.world_matrices.data(),
      sizeof(scene_data.world_matrices));
    std::array<PreparedSceneFrame::PartitionRange, 2> partitions {
      PreparedSceneFrame::PartitionRange {
        .pass_mask = scene_data.receiver_mask,
        .begin = 0U,
        .end = 1U,
      },
      PreparedSceneFrame::PartitionRange {
        .pass_mask = scene_data.shadow_caster_mask,
        .begin = 1U,
        .end = 3U,
      },
    };

    auto prepared_frame = PreparedSceneFrame {};
    prepared_frame.draw_metadata_bytes
      = std::as_bytes(std::span(scene_data.draw_records));
    prepared_frame.world_matrices = std::span<const float>(
      world_matrix_floats.data(), world_matrix_floats.size());
    prepared_frame.partitions = std::span(partitions);
    prepared_frame.draw_bounding_spheres = std::span(scene_data.draw_bounds);
    prepared_frame.shadow_caster_bounding_spheres
      = std::span(scene_data.shadow_caster_bounds);
    prepared_frame.visible_receiver_bounding_spheres
      = std::span(scene_data.receiver_bounds);
    prepared_frame.bindless_worlds_slot = world_allocation->srv;
    prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
    prepared_frame.bindless_draw_bounds_slot
      = scene_data.draw_bounds_buffer.slot;

    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame,
      MakeViewConstants(resolved_view, sequence, slot, view_frame_slot));

    auto depth_texture
      = CreateDepthTexture2D(width, height, "early-stage-two-box.depth");
    CHECK_NOTNULL_F(depth_texture.get(), "Failed to create depth texture");
    auto depth_pass = oxygen::engine::DepthPrePass(
      std::make_shared<oxygen::engine::DepthPrePass::Config>(
        oxygen::engine::DepthPrePass::Config {
          .depth_texture = depth_texture,
          .debug_name = "early-stage-two-box.depth-pass",
        }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);
    {
      auto recorder = AcquireRecorder("early-stage-two-box.depth-pass");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire depth recorder");
      EnsureTracked(
        *recorder, depth_texture, oxygen::graphics::ResourceStates::kCommon);
      RunPass(depth_pass, render_context, *recorder);
    }
    WaitForQueueIdle();

    auto lights = oxygen::renderer::LightManager(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer.GetStagingProvider() },
      oxygen::observer_ptr { &renderer.GetInlineTransfersCoordinator() });
    lights.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence, slot);
    CollectSceneLights(lights, scene_data);

    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence, slot);
    const auto has_work = vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, slot, view_frame_slot), lights,
      oxygen::observer_ptr<oxygen::scene::Scene> { scene_data.scene.get() },
      static_cast<float>(width), std::span(scene_data.rendered_items),
      std::span(scene_data.shadow_caster_bounds),
      std::span(scene_data.receiver_bounds), std::chrono::milliseconds { 16 },
      shadow_caster_content_hash);
    CHECK_F(has_work, "Two-box scene should produce virtual shadow work");
    ExecutePreparedViewShell(vsm_renderer, render_context,
      oxygen::observer_ptr<const oxygen::graphics::Texture> {
        depth_texture.get(),
      });

    return TwoBoxLiveFrameResult {
      .virtual_frame = vsm_renderer.GetVirtualAddressSpace().DescribeFrame(),
      .prepared_view = vsm_renderer.TryGetPreparedViewState(kTestViewId),
      .extracted_frame = vsm_renderer.GetCacheManager().GetPreviousFrame(),
    };
  }
};

} // namespace oxygen::renderer::vsm::testing
