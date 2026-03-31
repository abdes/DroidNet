//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <wrl/client.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
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
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageRequestGeneration.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace {

auto DredOpName(const D3D12_AUTO_BREADCRUMB_OP op) -> const char*
{
  switch (op) {
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
    return "DrawInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
    return "DrawIndexedInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
    return "ExecuteIndirect";
  case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
    return "Dispatch";
  case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
    return "CopyBufferRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
    return "CopyTextureRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
    return "CopyResource";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
    return "ClearUnorderedAccessView";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
    return "ClearDepthStencilView";
  case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
    return "ResourceBarrier";
  case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:
    return "BeginSubmission";
  case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:
    return "EndSubmission";
  default:
    return "Other";
  }
}

auto LogDredReport(ID3D12Device* device) -> void
{
  Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
  if (device == nullptr
    || FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
    LOG_F(WARNING, "DRED query unavailable");
    return;
  }

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs {};
  if (FAILED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))
    || breadcrumbs.pHeadAutoBreadcrumbNode == nullptr) {
    LOG_F(WARNING, "No DRED breadcrumbs available");
    return;
  }

  LOG_F(ERROR, "DRED breadcrumbs:");
  for (const auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr;
    node = node->pNext) {
    const auto* command_list_name = node->pCommandListDebugNameA != nullptr
      ? node->pCommandListDebugNameA
      : "Unnamed";
    auto last_breadcrumb = std::uint32_t { 0U };
    if (node->pLastBreadcrumbValue != nullptr) {
      last_breadcrumb = *node->pLastBreadcrumbValue;
    }
    auto last_op = "Unknown";
    if (node->pCommandHistory != nullptr && node->BreadcrumbCount > 0U) {
      const auto history_index
        = std::min<std::uint32_t>(last_breadcrumb, node->BreadcrumbCount - 1U);
      last_op = DredOpName(node->pCommandHistory[history_index]);
    }

    LOG_F(ERROR, "  CommandList={} breadcrumbs={} last_index={} last_op={}",
      command_list_name, node->BreadcrumbCount, last_breadcrumb, last_op);
    if (node->pBreadcrumbContexts != nullptr) {
      const auto contexts
        = std::span(node->pBreadcrumbContexts, node->BreadcrumbContextsCount);
      for (const auto& context : contexts) {
        if (context.pContextString == nullptr) {
          continue;
        }
        auto utf8_context = std::string {};
        oxygen::string_utils::WideToUtf8(context.pContextString, utf8_context);
        LOG_F(
          ERROR, "    Context[{}]={}", context.BreadcrumbIndex, utf8_context);
      }
    }
  }
}

using oxygen::Format;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::BindlessDrawMetadataSlot;
using oxygen::engine::BindlessViewFrameBindingsSlot;
using oxygen::engine::BindlessWorldsSlot;
using oxygen::engine::DepthPrePass;
using oxygen::engine::DrawFrameBindings;
using oxygen::engine::DrawMetadata;
using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ViewConstants;
using oxygen::engine::ViewFrameBindings;
using oxygen::engine::internal::PerViewStructuredPublisher;
using oxygen::engine::sceneprep::RenderItemData;
using oxygen::engine::sceneprep::TransformHandle;
using oxygen::engine::upload::TransientStructuredBuffer;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureUploadRegion;
using oxygen::renderer::LightManager;
using oxygen::renderer::vsm::BuildPageRequests;
using oxygen::renderer::vsm::kVsmInvalidLightIndex;
using oxygen::renderer::vsm::TryComputePageTableIndex;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageRequestFlagBits;
using oxygen::renderer::vsm::VsmShaderPageRequestFlags;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::VsmVisiblePixelSample;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

constexpr std::uint32_t kTextureUploadRowPitch = 256U;
constexpr ViewId kTestViewId { 91U };

struct TestVertex {
  glm::vec3 position {};
  glm::vec3 normal {};
  glm::vec2 texcoord {};
  glm::vec3 tangent {};
  glm::vec3 bitangent {};
  glm::vec4 color {};
};
static_assert(sizeof(TestVertex) == 72U);

struct ShaderVisibleBuffer {
  std::shared_ptr<Buffer> buffer {};
  ShaderVisibleIndex slot { oxygen::kInvalidShaderVisibleIndex };
};

struct AxisAlignedBox {
  glm::vec3 min {};
  glm::vec3 max {};
};

auto EncodeFloatTexel(const float value) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte>(kTextureUploadRowPitch, std::byte { 0 });
  std::memcpy(bytes.data(), &value, sizeof(value));
  return bytes;
}

auto DescribeNonZeroRequestFlags(const VsmShaderPageRequestFlags* flags,
  const std::uint32_t count) -> std::string
{
  auto result = std::string {};
  for (std::uint32_t index = 0; index < count; ++index) {
    if (flags[index].bits == 0U) {
      continue;
    }
    if (!result.empty()) {
      result += ", ";
    }
    result += std::to_string(index);
    result += "=";
    result += std::to_string(flags[index].bits);
  }
  return result.empty() ? std::string { "<none>" } : result;
}

auto BuildExpectedRequestFlags(
  const std::vector<VsmPageRequestProjection>& projections,
  const std::vector<VsmVisiblePixelSample>& samples,
  const std::uint32_t virtual_page_count)
  -> std::vector<VsmShaderPageRequestFlags>
{
  auto flags = std::vector<VsmShaderPageRequestFlags>(virtual_page_count);
  const auto requests = BuildPageRequests(projections, samples);
  for (const auto& request : requests) {
    auto index = std::optional<std::uint32_t> {};
    const VsmPageRequestProjection* matched_projection = nullptr;
    for (const auto& projection : projections) {
      if (projection.map_id != request.map_id) {
        continue;
      }

      index = TryComputePageTableIndex(projection, request.page);
      if (index.has_value()) {
        matched_projection = &projection;
        break;
      }
    }

    CHECK_F(index.has_value(), "Missing projection route for map_id {}",
      request.map_id);
    CHECK_NOTNULL_F(matched_projection, "Missing matched projection");
    CHECK_F(*index < virtual_page_count,
      "Expected request index {} exceeds virtual page count {}", *index,
      virtual_page_count);

    flags[*index].bits
      |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kRequired);
    const auto is_directional = matched_projection->projection.light_type
      == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional);
    if (!is_directional && request.page.level != 0U) {
      flags[*index].bits
        |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kCoarse);
    }
  }
  return flags;
}

class VsmShadowRendererBridgeGpuTest : public VsmCacheManagerGpuTestBase {
protected:
  [[nodiscard]] static auto AlignUp(
    const std::uint32_t value, const std::uint32_t alignment) -> std::uint32_t
  {
    CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
    return (value + alignment - 1U) / alignment * alignment;
  }

  [[nodiscard]] static auto MakeResolvedView(const glm::vec3 camera_position
    = glm::vec3 { 0.0F, 0.0F, 0.0F }) -> ResolvedView
  {
    const auto view_matrix = glm::lookAtRH(camera_position,
      camera_position + glm::vec3 { 0.0F, 0.0F, -1.0F },
      glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
        glm::radians(90.0F), 1.0F, 0.1F, 100.0F);
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1.0F,
      .height = 1.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = 1,
      .bottom = 1,
    };

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = view_matrix,
      .proj_matrix = projection_matrix,
      .camera_position = camera_position,
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeLookAtResolvedView(const glm::vec3 eye,
    const glm::vec3 target, const std::uint32_t width,
    const std::uint32_t height, const float fov_y_radians = glm::radians(60.0F))
    -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(width),
      .bottom = static_cast<std::int32_t>(height),
    };

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::lookAtRH(eye, target, glm::vec3 { 0.0F, 1.0F, 0.0F }),
      .proj_matrix
      = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(fov_y_radians,
        static_cast<float>(width) / static_cast<float>(height), 0.1F, 100.0F),
      .camera_position = eye,
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto RotationFromForwardTo(const glm::vec3 direction)
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

  [[nodiscard]] static auto ProjectWorldToPixel(
    const ResolvedView& resolved_view, const glm::vec3 world_position,
    const std::uint32_t width, const std::uint32_t height)
    -> std::optional<glm::uvec2>
  {
    const auto clip = resolved_view.ProjectionMatrix()
      * resolved_view.ViewMatrix() * glm::vec4(world_position, 1.0F);
    if (std::abs(clip.w) <= 1.0e-6F) {
      return std::nullopt;
    }

    const auto ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.0F || ndc.x > 1.0F || ndc.y < -1.0F || ndc.y > 1.0F
      || ndc.z < 0.0F || ndc.z > 1.0F) {
      return std::nullopt;
    }

    const auto pixel_x = static_cast<std::uint32_t>(
      std::clamp((ndc.x * 0.5F + 0.5F) * static_cast<float>(width), 0.0F,
        static_cast<float>(width - 1U)));
    const auto pixel_y = static_cast<std::uint32_t>(
      std::clamp((1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(height),
        0.0F, static_cast<float>(height - 1U)));
    return glm::uvec2 { pixel_x, pixel_y };
  }

  [[nodiscard]] static auto RayIntersectsAabb(const glm::vec3 origin,
    const glm::vec3 direction, const glm::vec3 box_min, const glm::vec3 box_max,
    const float max_distance = std::numeric_limits<float>::infinity()) -> bool
  {
    auto t_min = 0.0F;
    auto t_max = max_distance;

    for (auto axis = 0; axis < 3; ++axis) {
      const auto dir = direction[axis];
      if (std::abs(dir) <= 1.0e-6F) {
        if (origin[axis] < box_min[axis] || origin[axis] > box_max[axis]) {
          return false;
        }
        continue;
      }

      auto t1 = (box_min[axis] - origin[axis]) / dir;
      auto t2 = (box_max[axis] - origin[axis]) / dir;
      if (t1 > t2) {
        std::swap(t1, t2);
      }
      t_min = std::max(t_min, t1);
      t_max = std::min(t_max, t2);
      if (t_min > t_max) {
        return false;
      }
    }

    return t_max >= std::max(0.0F, t_min);
  }

  [[nodiscard]] static auto MakeIdentityResolvedView(
    const std::uint32_t width, const std::uint32_t height) -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(width),
      .bottom = static_cast<std::int32_t>(height),
    };

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = glm::mat4 { 1.0F },
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto ComputeDepthForWorldPoint(
    const ResolvedView& resolved_view, const glm::vec3 world_position) -> float
  {
    const auto clip = resolved_view.ProjectionMatrix()
      * resolved_view.ViewMatrix() * glm::vec4(world_position, 1.0F);
    return clip.z / clip.w;
  }

  auto UploadSingleChannelTexture(
    const float value, std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto texture = CreateDepthTexture2D(1U, 1U, debug_name);
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kTextureUploadRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    const auto upload_bytes = EncodeFloatTexel(value);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    const auto upload_region = TextureUploadRegion {
      .buffer_offset = 0U,
      .buffer_row_pitch = kTextureUploadRowPitch,
      .buffer_slice_pitch = kTextureUploadRowPitch,
      .dst_slice = {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = 1U,
        .height = 1U,
        .depth = 1U,
        .mip_level = 0U,
        .array_slice = 0U,
      },
    };

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Init");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload, upload_region, *texture);
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  auto UploadFilledSingleChannelTexture(const std::uint32_t width,
    const std::uint32_t height, const float value, std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture = CreateDepthTexture2D(width, height, debug_name);
    CHECK_NOTNULL_F(texture.get(), "Failed to create texture `{}`", debug_name);

    const auto row_pitch
      = AlignUp(width * sizeof(float), kTextureUploadRowPitch);
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(
      upload.get(), "Failed to create upload buffer `{}`", debug_name);

    auto upload_bytes
      = std::vector<std::byte>(row_pitch * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * row_pitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Init");
      CHECK_NOTNULL_F(recorder.get(),
        "Failed to acquire upload recorder for `{}`", debug_name);
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        TextureUploadRegion {
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
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  auto CreateDepthTexture2D(const std::uint32_t width,
    const std::uint32_t height, std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture_desc = TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
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

  template <typename T>
  auto CreateStructuredSrvBuffer(std::span<const T> elements,
    std::string_view debug_name) -> ShaderVisibleBuffer
  {
    CHECK_F(
      !elements.empty(), "Structured SRV buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);
    UploadBufferBytes(
      buffer, elements.data(), elements.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate structured SRV for `{}`",
      debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .range
      = oxygen::graphics::BufferRange {
          0U,
          static_cast<std::uint64_t>(elements.size_bytes()),
        },
      .stride = static_cast<std::uint32_t>(sizeof(T)),
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register structured SRV for `{}`",
      debug_name);

    return ShaderVisibleBuffer { .buffer = std::move(buffer), .slot = slot };
  }

  auto CreateUIntIndexBuffer(std::span<const std::uint32_t> indices,
    std::string_view debug_name) -> ShaderVisibleBuffer
  {
    CHECK_F(!indices.empty(), "Index buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(indices.size_bytes()),
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);
    UploadBufferBytes(buffer, indices.data(), indices.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kRawBuffer_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(
      handle.IsValid(), "Failed to allocate raw SRV for `{}`", debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = ResourceViewType::kRawBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .format = Format::kR32UInt,
      .range
      = oxygen::graphics::BufferRange {
          0U,
          static_cast<std::uint64_t>(indices.size_bytes()),
        },
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register raw SRV for `{}`", debug_name);

    return ShaderVisibleBuffer { .buffer = std::move(buffer), .slot = slot };
  }

  auto ReadOutputTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, std::string_view debug_name)
    -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read a null output texture");

    auto readback = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kTextureUploadRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire probe recorder");
      EnsureTracked(*recorder, std::const_pointer_cast<Texture>(texture),
        ResourceStates::kCommon);
      EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);
      recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
      recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch = oxygen::SizeBytes { kTextureUploadRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        });
    }
    WaitForQueueIdle();

    auto value = 0.0F;
    const auto* mapped = static_cast<const std::byte*>(
      readback->Map(0U, kTextureUploadRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  [[nodiscard]] static auto MakeLocalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const ResolvedView& resolved_view) -> VsmPageRequestProjection
  {
    const auto& layout = frame.local_light_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = layout.id,
      .first_page_table_entry = layout.first_page_table_entry,
      .map_pages_x = layout.pages_per_level_x,
      .map_pages_y = layout.pages_per_level_y,
      .pages_x = layout.pages_per_level_x,
      .pages_y = layout.pages_per_level_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = layout.level_count,
      .coarse_level = 0U,
      .light_index = kVsmInvalidLightIndex,
    };
  }

  [[nodiscard]] static auto MakeDirectionalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const ResolvedView& resolved_view) -> VsmPageRequestProjection
  {
    const auto& layout = frame.directional_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = layout.first_id,
      .first_page_table_entry = layout.first_page_table_entry,
      .map_pages_x = layout.pages_per_axis,
      .map_pages_y = layout.pages_per_axis,
      .pages_x = layout.pages_per_axis,
      .pages_y = layout.pages_per_axis,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = layout.clip_level_count,
      .coarse_level = 0U,
    };
  }

  [[nodiscard]] static auto MakeViewConstants(const ResolvedView& resolved_view,
    const SequenceNumber sequence, const Slot slot,
    const ShaderVisibleIndex view_frame_slot = ShaderVisibleIndex { 1U })
    -> ViewConstants
  {
    auto view_constants = ViewConstants {};
    view_constants.SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition())
      .SetFrameSlot(slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot { view_frame_slot },
        ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer);
    return view_constants;
  }

  static auto UpdateTransforms(Scene& scene, SceneNode& node) -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(scene);
  }

  [[nodiscard]] static auto MakeCurrentSeam(VsmShadowRenderer& vsm_renderer)
    -> oxygen::renderer::vsm::VsmCacheManagerSeam
  {
    const auto& current_frame
      = vsm_renderer.GetVirtualAddressSpace().DescribeFrame();
    const auto* previous_frame
      = vsm_renderer.GetCacheManager().GetPreviousFrame();
    return oxygen::renderer::vsm::VsmCacheManagerSeam {
      .physical_pool
      = vsm_renderer.GetPhysicalPagePoolManager().GetShadowPoolSnapshot(),
      .hzb_pool
      = vsm_renderer.GetPhysicalPagePoolManager().GetHzbPoolSnapshot(),
      .current_frame = current_frame,
      .previous_to_current_remap = previous_frame != nullptr
        ? vsm_renderer.GetVirtualAddressSpace().BuildRemapTable(
            previous_frame->virtual_frame)
        : oxygen::renderer::vsm::VsmVirtualRemapTable {},
    };
  }

  static auto CollectDirectionalLight(LightManager& lights, SceneNode& node)
    -> oxygen::engine::DirectionalShadowCandidate
  {
    auto impl = node.GetImpl();
    if (!impl.has_value()) {
      ADD_FAILURE() << "directional light node has no implementation";
      return {};
    }
    lights.CollectFromNode(node.GetHandle(), impl->get());
    const auto candidates = lights.GetDirectionalShadowCandidates();
    EXPECT_EQ(candidates.size(), 1U);
    return candidates.empty() ? oxygen::engine::DirectionalShadowCandidate {}
                              : candidates.front();
  }

  [[nodiscard]] static auto MakeDirectionalPageRequest(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame)
    -> oxygen::renderer::vsm::VsmPageRequest
  {
    const auto& layout = frame.directional_layouts[0];
    return oxygen::renderer::vsm::VsmPageRequest {
      .map_id = layout.first_id,
      .page = { .level = 0U, .page_x = 0U, .page_y = 0U },
      .flags = oxygen::renderer::vsm::VsmPageRequestFlags::kRequired,
    };
  }

  auto ExecutePreparedViewShell(VsmShadowRenderer& renderer,
    const oxygen::engine::RenderContext& render_context,
    const std::shared_ptr<Texture>& scene_depth_texture = {}) -> void
  {
    if (const auto* depth_pass = render_context.GetPass<DepthPrePass>();
      depth_pass != nullptr && scene_depth_texture != nullptr) {
      auto depth_recorder = AcquireRecorder("phase-ka5-live-shell.depth-pass");
      ASSERT_NE(depth_recorder, nullptr);
      if (depth_recorder != nullptr) {
        EnsureTracked(
          *depth_recorder, scene_depth_texture, ResourceStates::kCommon);
        EventLoop publish_loop;
        oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
          co_await const_cast<DepthPrePass&>(*depth_pass)
            .PrepareResources(render_context, *depth_recorder);
        });
      }
      WaitForQueueIdle();
    }

    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      auto recorder = Backend().AcquireCommandRecorder(
        Backend().QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
        "phase-ka5-live-shell");
      EXPECT_NE(recorder.get(), nullptr);
      if (recorder == nullptr) {
        co_return;
      }
      co_await renderer.ExecutePreparedViewShell(render_context, *recorder,
        oxygen::observer_ptr<const Texture> { scene_depth_texture.get() });
    });
    const auto completed_value = WaitForQueueIdle();
    if (completed_value.get() == std::numeric_limits<std::uint64_t>::max()) {
      auto* device = const_cast<oxygen::graphics::d3d12::dx::IDevice*>(
        Backend().GetCurrentDevice());
      ASSERT_NE(device, nullptr);
      const auto reason = device->GetDeviceRemovedReason();
      LOG_F(ERROR,
        "VSM bridge live shell detected device removal after queue wait; "
        "GetDeviceRemovedReason=0x{:08X}",
        static_cast<std::uint32_t>(reason));
      LogDredReport(device);
    }
  }

  auto CreateMetadataSeedBuffer(
    const std::span<const oxygen::renderer::vsm::VsmPhysicalPageMeta> metadata,
    std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(metadata.size())
        * sizeof(oxygen::renderer::vsm::VsmPhysicalPageMeta),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create metadata seed buffer");
    UploadBufferBytes(buffer, metadata.data(),
      metadata.size() * sizeof(oxygen::renderer::vsm::VsmPhysicalPageMeta),
      debug_name);
    return buffer;
  }
};

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PageRequestReadbackBridgeCommitsAllocationFrameFromGpuRequests)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture = UploadSingleChannelTexture(depth, "phase-ka4.depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto& pool_manager = vsm_renderer.GetPhysicalPagePoolManager();
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-ka4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-ka4-hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 1ULL, 11U, "phase-ka4-frame");
  const auto projection_record
    = MakeLocalProjectionRecord(seam.current_frame, resolved_view);
  const auto projection_records = std::array { projection_record };

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);
  {
    auto depth_recorder = AcquireRecorder("phase-ka4.depth-pass");
    ASSERT_NE(depth_recorder, nullptr);
    EnsureTracked(*depth_recorder, depth_texture, ResourceStates::kCommon);
    EventLoop publish_loop;
    oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
      co_await depth_pass.PrepareResources(render_context, *depth_recorder);
    });
  }
  WaitForQueueIdle();

  auto bridge_committed_requests = false;
  EventLoop loop;
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    bridge_committed_requests
      = co_await vsm_renderer.ExecutePageRequestReadbackBridge(
        render_context, seam, projection_records);
  });
  WaitForQueueIdle();

  EXPECT_TRUE(bridge_committed_requests);

  const auto* committed_frame
    = vsm_renderer.GetCacheManager().GetCurrentFrame();
  ASSERT_NE(committed_frame, nullptr);
  ASSERT_TRUE(committed_frame->is_ready);
  ASSERT_EQ(committed_frame->snapshot.projection_records.size(), 1U);
  EXPECT_EQ(committed_frame->snapshot.projection_records[0], projection_record);
  ASSERT_EQ(committed_frame->plan.decisions.size(), 1U);
  EXPECT_EQ(committed_frame->plan.allocated_page_count, 1U);
  EXPECT_EQ(committed_frame->plan.decisions[0].action,
    VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.map_id,
    seam.current_frame.local_light_layouts[0].id);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.level, 0U);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.page_x, 0U);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.page_y, 0U);

  const auto page_table_entry
    = TryGetPageTableEntryIndex(seam.current_frame.local_light_layouts[0], {});
  ASSERT_TRUE(page_table_entry.has_value());
  ASSERT_LT(*page_table_entry, committed_frame->snapshot.page_table.size());
  ASSERT_TRUE(
    committed_frame->snapshot.page_table[*page_table_entry].is_mapped);
  EXPECT_EQ(
    committed_frame->snapshot.page_table[*page_table_entry].physical_page,
    committed_frame->plan.decisions[0].current_physical_page);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellRunsStageSixThroughProjectionAndExtractsReadyFrame)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka5-live-shell.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka5-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  light.CascadedShadows().cascade_count = 4U;
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto frame_config = Renderer::OffscreenFrameConfig {
    .frame_slot = Slot { 0U },
    .frame_sequence = SequenceNumber { 1U },
    .scene = oxygen::observer_ptr<Scene> { scene.get() },
  };
  auto offscreen = renderer->BeginOffscreenFrame(frame_config);
  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  auto lights = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
  lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    SequenceNumber { 1U }, Slot { 0U });
  static_cast<void>(CollectDirectionalLight(lights, sun_node));

  vsm_renderer.OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    SequenceNumber { 1U }, Slot { 0U });
  static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
    MakeViewConstants(resolved_view, SequenceNumber { 1U }, Slot { 0U }),
    lights, oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
  ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);

  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);

  const auto* extracted_frame
    = vsm_renderer.GetCacheManager().GetPreviousFrame();
  ASSERT_NE(extracted_frame, nullptr);
  EXPECT_FALSE(extracted_frame->projection_records.empty());
  EXPECT_TRUE(extracted_frame->is_hzb_data_available);

  auto directional_projection_count = std::size_t { 0U };
  for (const auto& projection : extracted_frame->projection_records) {
    if (projection.projection.light_type
      != static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
      continue;
    }
    ++directional_projection_count;
    EXPECT_GT(projection.level_count, projection.projection.clipmap_level);
  }
  EXPECT_EQ(directional_projection_count, 4U);

  const auto projection_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  EXPECT_TRUE(projection_output.available);
  EXPECT_NE(
    projection_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
}

#if 0
// Stage 15 projection correctness is owned by VsmShadowProjection_test.cpp.
// These legacy bridge tests hand-build duplicate Stage 15 setups and are no
// longer part of the lifecycle binary's ownership boundary.
NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellProjectsLocalizedDirectionalMaskForRasterizedCasters)
{
  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 21U };
  constexpr auto kOutputWidth = 64U;
  constexpr auto kOutputHeight = 64U;

  const auto resolved_view = MakeResolvedView();
  const auto receiver_world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto receiver_depth
    = ComputeDepthForWorldPoint(resolved_view, receiver_world_position);
  auto depth_texture = UploadFilledSingleChannelTexture(kOutputWidth,
    kOutputHeight, receiver_depth, "phase-k-shell-localized.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-k-shell-localized-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  light.CascadedShadows().cascade_count = 1U;
  const auto sun_rotation
    = glm::angleAxis(glm::radians(90.0F), glm::vec3 { 1.0F, 0.0F, 0.0F });
  ASSERT_TRUE(sun_node.GetTransform().SetLocalRotation(sun_rotation));
  UpdateTransforms(*scene, sun_node);

  auto caster_a_node = scene->CreateNode("caster-a", sun_flags);
  auto caster_b_node = scene->CreateNode("caster-b", sun_flags);
  ASSERT_TRUE(caster_a_node.IsValid());
  ASSERT_TRUE(caster_b_node.IsValid());

  std::array<TestVertex, 4> vertices {
    TestVertex {
      .position = { -0.5F, -0.5F, 0.0F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.5F, -0.5F, 0.0F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.5F, 0.5F, 0.0F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
    TestVertex {
      .position = { -0.5F, 0.5F, 0.0F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 1.0F, 0.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 6> kIndices {
    0U,
    1U,
    2U,
    0U,
    2U,
    3U,
  };

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-k-shell-localized.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-k-shell-localized.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-shell-localized.worlds");
  world_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(2U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));
  const std::array<glm::mat4, 2> world_matrices {
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { -1.25F, 0.0F, -4.5F }),
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 1.25F, 0.0F, -4.5F }),
  };
  std::memcpy(world_allocation->mapped_ptr, world_matrices.data(),
    sizeof(world_matrices));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-shell-localized.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(2U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);
  shadow_caster_mask.Set(PassMaskBit::kMainViewVisible);
  constexpr auto kMainViewVisiblePrimitiveFlag
    = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);

  std::array<DrawMetadata, 2> draw_records {
    DrawMetadata {
      .vertex_buffer_index = vertex_buffer.slot,
      .index_buffer_index = index_buffer.slot,
      .first_index = 0U,
      .base_vertex = 0,
      .is_indexed = 1U,
      .instance_count = 1U,
      .index_count = static_cast<std::uint32_t>(kIndices.size()),
      .vertex_count = 0U,
      .material_handle = 0U,
      .transform_index = 0U,
      .instance_metadata_buffer_index = 0U,
      .instance_metadata_offset = 0U,
      .flags = shadow_caster_mask,
      .transform_generation = 31U,
      .submesh_index = 0U,
      .primitive_flags = kMainViewVisiblePrimitiveFlag,
    },
    DrawMetadata {
      .vertex_buffer_index = vertex_buffer.slot,
      .index_buffer_index = index_buffer.slot,
      .first_index = 0U,
      .base_vertex = 0,
      .is_indexed = 1U,
      .instance_count = 1U,
      .index_count = static_cast<std::uint32_t>(kIndices.size()),
      .vertex_count = 0U,
      .material_handle = 0U,
      .transform_index = 1U,
      .instance_metadata_buffer_index = 0U,
      .instance_metadata_offset = 0U,
      .flags = shadow_caster_mask,
      .transform_generation = 32U,
      .submesh_index = 0U,
      .primitive_flags = kMainViewVisiblePrimitiveFlag,
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 2> draw_bounds {
    glm::vec4 { -1.25F, 0.0F, -4.5F, 0.75F },
    glm::vec4 { 1.25F, 0.0F, -4.5F, 0.75F },
  };
  const std::array<glm::vec4, 1> receiver_bounds {
    glm::vec4 { 0.0F, 0.0F, -5.0F, 6.0F },
  };
  const std::array<RenderItemData, 2> rendered_items {
    RenderItemData {
      .submesh_index = 0U,
      .node_handle = caster_a_node.GetHandle(),
      .world_bounding_sphere = draw_bounds[0],
      .transform_handle = TransformHandle {
        TransformHandle::Index { 0U },
        TransformHandle::Generation { 31U },
      },
      .cast_shadows = true,
      .receive_shadows = false,
      .main_view_visible = true,
      .static_shadow_caster = false,
    },
    RenderItemData {
      .submesh_index = 0U,
      .node_handle = caster_b_node.GetHandle(),
      .world_bounding_sphere = draw_bounds[1],
      .transform_handle = TransformHandle {
        TransformHandle::Index { 1U },
        TransformHandle::Generation { 32U },
      },
      .cast_shadows = true,
      .receive_shadows = false,
      .main_view_visible = true,
      .static_shadow_caster = false,
    },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-k-shell-localized.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-shell-localized.DrawFrameBindings");
  draw_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto draw_frame_slot = draw_frame_publisher.Publish(kTestViewId,
    DrawFrameBindings {
      .draw_metadata_slot = BindlessDrawMetadataSlot(draw_allocation->srv),
      .transforms_slot = BindlessWorldsSlot(world_allocation->srv),
    });
  ASSERT_TRUE(draw_frame_slot.IsValid());

  auto view_frame_publisher = PerViewStructuredPublisher<ViewFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-shell-localized.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  auto world_matrix_floats = std::array<float, 32> {};
  std::memcpy(
    world_matrix_floats.data(), world_matrices.data(), sizeof(world_matrices));

  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 2U,
    },
  };

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draw_records));
  prepared_frame.world_matrices = std::span<const float>(
    world_matrix_floats.data(), world_matrix_floats.size());
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.shadow_caster_bounding_spheres = std::span(draw_bounds);
  prepared_frame.visible_receiver_bounding_spheres = std::span(receiver_bounds);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
  prepared_frame.bindless_draw_bounds_slot = draw_bounds_buffer.slot;

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto frame_config = Renderer::OffscreenFrameConfig {
    .frame_slot = kFrameSlot,
    .frame_sequence = kFrameSequence,
    .scene = oxygen::observer_ptr<Scene> { scene.get() },
  };
  auto offscreen = renderer->BeginOffscreenFrame(frame_config);
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame,
    MakeViewConstants(
      resolved_view, kFrameSequence, kFrameSlot, view_frame_slot));

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  auto lights = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
  lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    kFrameSequence, kFrameSlot);
  static_cast<void>(CollectDirectionalLight(lights, sun_node));

  vsm_renderer.OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(), kFrameSequence,
    kFrameSlot);
  static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
    MakeViewConstants(
      resolved_view, kFrameSequence, kFrameSlot, view_frame_slot),
    lights, oxygen::observer_ptr<Scene> { scene.get() },
    static_cast<float>(kOutputWidth), std::span(rendered_items),
    std::span(draw_bounds), std::span(receiver_bounds),
    std::chrono::milliseconds { 16 }, 0xCA57ULL));
  ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);

  const auto visible_primitives
    = vsm_renderer.GetShadowRasterizerPass()->GetVisibleShadowPrimitives();
  EXPECT_EQ(visible_primitives.size(), rendered_items.size());

  const auto output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);

  const auto left_center
    = ReadOutputTexel(output.directional_shadow_mask_texture, 24U, 32U,
      "phase-k-shell-localized.mask.left-center");
  const auto right_center
    = ReadOutputTexel(output.directional_shadow_mask_texture, 40U, 32U,
      "phase-k-shell-localized.mask.right-center");
  const auto left_edge = ReadOutputTexel(output.directional_shadow_mask_texture,
    6U, 32U, "phase-k-shell-localized.mask.left-edge");
  const auto right_edge
    = ReadOutputTexel(output.directional_shadow_mask_texture, 58U, 32U,
      "phase-k-shell-localized.mask.right-edge");
  const auto top_left = ReadOutputTexel(output.directional_shadow_mask_texture,
    6U, 6U, "phase-k-shell-localized.mask.top-left");
  const auto bottom_right
    = ReadOutputTexel(output.directional_shadow_mask_texture, 58U, 58U,
      "phase-k-shell-localized.mask.bottom-right");

  EXPECT_LT(left_center, 0.1F);
  EXPECT_LT(right_center, 0.1F);
  EXPECT_NEAR(left_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(right_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(top_left, 1.0F, 1.0e-4F);
  EXPECT_NEAR(bottom_right, 1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellMatchesAnalyticFloorShadowClassificationForTwoBoxes)
{
  struct ProbeSample {
    glm::vec3 point_ws {};
    glm::uvec2 pixel {};
  };

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 31U };
  constexpr auto kOutputWidth = 256U;
  constexpr auto kOutputHeight = 256U;
  constexpr auto kMainViewVisiblePrimitiveFlag
    = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto resolved_view = MakeLookAtResolvedView(
    camera_eye, camera_target, kOutputWidth, kOutputHeight);
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto inverse_view_projection = glm::inverse(
    resolved_view.ProjectionMatrix() * resolved_view.ViewMatrix());

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-k-two-box-floor-scene", 32);
  const auto visible_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", visible_flags);
  auto floor_node = scene->CreateNode("floor", visible_flags);
  auto tall_box_node = scene->CreateNode("tall-box", visible_flags);
  auto short_box_node = scene->CreateNode("short-box", visible_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(floor_node.IsValid());
  ASSERT_TRUE(tall_box_node.IsValid());
  ASSERT_TRUE(short_box_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));

  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& sun_light = sun_impl->get().GetComponent<DirectionalLight>();
  sun_light.Common().casts_shadows = true;
  sun_light.SetIsSunLight(true);
  sun_light.CascadedShadows().cascade_count = 1U;
  ASSERT_TRUE(sun_node.GetTransform().SetLocalRotation(
    RotationFromForwardTo(sun_direction)));
  UpdateTransforms(*scene, sun_node);

  auto make_vertex = [](const glm::vec3 position) {
    return TestVertex {
      .position = position,
      .normal = { 0.0F, 1.0F, 0.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    };
  };

  const std::array<TestVertex, 4> floor_vertices {
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

  const std::array<TestVertex, 8> cube_vertices {
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

  auto floor_vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    floor_vertices, "phase-k-two-box-floor.floor.vertices");
  auto floor_index_buffer = CreateUIntIndexBuffer(
    kFloorIndices, "phase-k-two-box-floor.floor.indices");
  auto cube_vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    cube_vertices, "phase-k-two-box-floor.cube.vertices");
  auto cube_index_buffer
    = CreateUIntIndexBuffer(kCubeIndices, "phase-k-two-box-floor.cube.indices");

  auto frame_config = Renderer::OffscreenFrameConfig {
    .frame_slot = kFrameSlot,
    .frame_sequence = kFrameSequence,
    .scene = oxygen::observer_ptr<Scene> { scene.get() },
  };
  auto offscreen = renderer->BeginOffscreenFrame(frame_config);

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-two-box-floor.worlds");
  world_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(3U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));

  const auto floor_world = glm::mat4 { 1.0F };
  const auto tall_box_world
    = glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 0.95F, 0.0F, -0.25F })
    * glm::scale(glm::mat4 { 1.0F }, glm::vec3 { 0.8F, 3.2F, 0.8F });
  const auto short_box_world
    = glm::translate(glm::mat4 { 1.0F }, glm::vec3 { -0.55F, 0.0F, 0.65F })
    * glm::scale(glm::mat4 { 1.0F }, glm::vec3 { 0.8F, 1.1F, 0.8F });
  const std::array<glm::mat4, 3> world_matrices {
    floor_world,
    tall_box_world,
    short_box_world,
  };
  std::memcpy(world_allocation->mapped_ptr, world_matrices.data(),
    sizeof(world_matrices));

  auto main_view_mask = PassMask {};
  main_view_mask.Set(PassMaskBit::kOpaque);
  main_view_mask.Set(PassMaskBit::kMainViewVisible);
  auto receiver_mask = main_view_mask;
  receiver_mask.Set(PassMaskBit::kDoubleSided);
  auto shadow_caster_mask = main_view_mask;
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 3> draw_records {
    DrawMetadata {
      .vertex_buffer_index = floor_vertex_buffer.slot,
      .index_buffer_index = floor_index_buffer.slot,
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
      .flags = receiver_mask,
      .transform_generation = 41U,
      .submesh_index = 0U,
      .primitive_flags = kMainViewVisiblePrimitiveFlag,
    },
    DrawMetadata {
      .vertex_buffer_index = cube_vertex_buffer.slot,
      .index_buffer_index = cube_index_buffer.slot,
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
      .flags = shadow_caster_mask,
      .transform_generation = 42U,
      .submesh_index = 0U,
      .primitive_flags = kMainViewVisiblePrimitiveFlag,
    },
    DrawMetadata {
      .vertex_buffer_index = cube_vertex_buffer.slot,
      .index_buffer_index = cube_index_buffer.slot,
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
      .flags = shadow_caster_mask,
      .transform_generation = 43U,
      .submesh_index = 0U,
      .primitive_flags = kMainViewVisiblePrimitiveFlag,
    },
  };
  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-two-box-floor.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(
    static_cast<std::uint32_t>(draw_records.size()));
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence));
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const AxisAlignedBox tall_box {
    .min = glm::vec3 { 0.55F, 0.0F, -0.65F },
    .max = glm::vec3 { 1.35F, 3.2F, 0.15F },
  };
  const AxisAlignedBox short_box {
    .min = glm::vec3 { -0.95F, 0.0F, 0.25F },
    .max = glm::vec3 { -0.15F, 1.1F, 1.05F },
  };
  const AxisAlignedBox floor_bounds {
    .min = glm::vec3 { -4.5F, -0.01F, -4.5F },
    .max = glm::vec3 { 4.5F, 0.01F, 4.5F },
  };
  const std::array<glm::vec4, 3> draw_bounds {
    glm::vec4 { 0.0F, 0.0F, 0.0F, 6.5F },
    glm::vec4 { 0.95F, 1.6F, -0.25F, 1.75F },
    glm::vec4 { -0.55F, 0.55F, 0.65F, 0.95F },
  };
  const std::array<glm::vec4, 2> shadow_caster_bounds {
    draw_bounds[1],
    draw_bounds[2],
  };
  const std::array<glm::vec4, 1> receiver_bounds {
    draw_bounds[0],
  };
  const std::array<RenderItemData, 3> rendered_items {
    RenderItemData {
      .submesh_index = 0U,
      .node_handle = floor_node.GetHandle(),
      .world_bounding_sphere = draw_bounds[0],
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
      .node_handle = tall_box_node.GetHandle(),
      .world_bounding_sphere = draw_bounds[1],
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
      .node_handle = short_box_node.GetHandle(),
      .world_bounding_sphere = draw_bounds[2],
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
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-k-two-box-floor.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-two-box-floor.DrawFrameBindings");
  draw_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto draw_frame_slot = draw_frame_publisher.Publish(kTestViewId,
    DrawFrameBindings {
      .draw_metadata_slot = BindlessDrawMetadataSlot(draw_allocation->srv),
      .transforms_slot = BindlessWorldsSlot(world_allocation->srv),
    });
  ASSERT_TRUE(draw_frame_slot.IsValid());

  auto view_frame_publisher = PerViewStructuredPublisher<ViewFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-k-two-box-floor.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  auto world_matrix_floats = std::array<float, 48> {};
  std::memcpy(
    world_matrix_floats.data(), world_matrices.data(), sizeof(world_matrices));
  std::array<PreparedSceneFrame::PartitionRange, 2> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = receiver_mask,
      .begin = 0U,
      .end = 1U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 1U,
      .end = 3U,
    },
  };

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draw_records));
  prepared_frame.world_matrices = std::span<const float>(
    world_matrix_floats.data(), world_matrix_floats.size());
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.shadow_caster_bounding_spheres
    = std::span(shadow_caster_bounds);
  prepared_frame.visible_receiver_bounding_spheres = std::span(receiver_bounds);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
  prepared_frame.bindless_draw_bounds_slot = draw_bounds_buffer.slot;

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame,
    MakeViewConstants(
      resolved_view, kFrameSequence, kFrameSlot, view_frame_slot));

  auto depth_texture = CreateDepthTexture2D(
    kOutputWidth, kOutputHeight, "phase-k-two-box-floor.depth");
  ASSERT_NE(depth_texture, nullptr);
  auto depth_pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "phase-k-two-box-floor.depth-pass",
    }));
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("phase-k-two-box-floor.depth-pass");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
    RunPass(depth_pass, render_context, *recorder);
    recorder->RequireResourceStateFinal(*depth_texture, ResourceStates::kCommon);
    recorder->FlushBarriers();
  }
  WaitForQueueIdle();

  auto lights = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
  lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    kFrameSequence, kFrameSlot);
  static_cast<void>(CollectDirectionalLight(lights, sun_node));

  vsm_renderer.OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(), kFrameSequence,
    kFrameSlot);
  static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
    MakeViewConstants(
      resolved_view, kFrameSequence, kFrameSlot, view_frame_slot),
    lights, oxygen::observer_ptr<Scene> { scene.get() },
    static_cast<float>(kOutputWidth), std::span(rendered_items),
    std::span(shadow_caster_bounds), std::span(receiver_bounds),
    std::chrono::milliseconds { 16 }, 0xB0A5ULL));
  ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);

  auto raycast_distance_to_aabb
    = [](const glm::vec3 origin, const glm::vec3 direction,
        const AxisAlignedBox& box) -> std::optional<float> {
    auto t_min = 0.0F;
    auto t_max = std::numeric_limits<float>::infinity();
    for (auto axis = 0; axis < 3; ++axis) {
      const auto dir = direction[axis];
      if (std::abs(dir) < 1.0e-6F) {
        if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
          return std::nullopt;
        }
        continue;
      }

      auto t0 = (box.min[axis] - origin[axis]) / dir;
      auto t1 = (box.max[axis] - origin[axis]) / dir;
      if (t0 > t1) {
        std::swap(t0, t1);
      }
      t_min = std::max(t_min, t0);
      t_max = std::min(t_max, t1);
      if (t_min > t_max) {
        return std::nullopt;
      }
    }

    return t_max >= t_min ? std::optional<float> { t_min } : std::nullopt;
  };
  auto raycast_distance_to_floor
    = [&](const glm::vec3 origin,
        const glm::vec3 direction) -> std::optional<float> {
    if (std::abs(direction.y) < 1.0e-6F) {
      return std::nullopt;
    }
    const auto distance = -origin.y / direction.y;
    if (distance <= 0.0F) {
      return std::nullopt;
    }
    const auto hit = origin + direction * distance;
    if (hit.x < floor_bounds.min.x || hit.x > floor_bounds.max.x
      || hit.z < floor_bounds.min.z || hit.z > floor_bounds.max.z) {
      return std::nullopt;
    }
    return distance;
  };
  auto pixel_center_ray
    = [&](const std::uint32_t x,
        const std::uint32_t y) -> std::pair<glm::vec3, glm::vec3> {
    const auto ndc_x = (2.0F * (static_cast<float>(x) + 0.5F)
                         / static_cast<float>(kOutputWidth))
      - 1.0F;
    const auto ndc_y = 1.0F
      - (2.0F * (static_cast<float>(y) + 0.5F)
        / static_cast<float>(kOutputHeight));
    auto near_point
      = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 0.0F, 1.0F };
    auto far_point
      = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 1.0F, 1.0F };
    near_point /= near_point.w;
    far_point /= far_point.w;
    const auto origin = glm::vec3 { near_point };
    const auto direction = glm::normalize(glm::vec3 { far_point - near_point });
    return { origin, direction };
  };

  auto visible_floor_pixels = std::vector<ProbeSample> {};
  visible_floor_pixels.reserve(kOutputWidth * kOutputHeight);
  for (std::uint32_t y = 0U; y < kOutputHeight; ++y) {
    for (std::uint32_t x = 0U; x < kOutputWidth; ++x) {
      const auto [ray_origin, ray_direction] = pixel_center_ray(x, y);
      auto nearest_distance = std::numeric_limits<float>::infinity();
      auto hit_point = std::optional<glm::vec3> {};
      auto hit_floor = false;

      if (const auto floor_distance
        = raycast_distance_to_floor(ray_origin, ray_direction);
        floor_distance.has_value() && *floor_distance < nearest_distance) {
        nearest_distance = *floor_distance;
        hit_point = ray_origin + ray_direction * *floor_distance;
        hit_floor = true;
      }
      if (const auto tall_distance
        = raycast_distance_to_aabb(ray_origin, ray_direction, tall_box);
        tall_distance.has_value() && *tall_distance < nearest_distance) {
        nearest_distance = *tall_distance;
        hit_point = ray_origin + ray_direction * *tall_distance;
        hit_floor = false;
      }
      if (const auto short_distance
        = raycast_distance_to_aabb(ray_origin, ray_direction, short_box);
        short_distance.has_value() && *short_distance < nearest_distance) {
        nearest_distance = *short_distance;
        hit_point = ray_origin + ray_direction * *short_distance;
        hit_floor = false;
      }

      if (hit_point.has_value()) {
        if (hit_floor) {
          visible_floor_pixels.push_back(
            ProbeSample { .point_ws = *hit_point, .pixel = { x, y } });
        }
      }
    }
  }

  ASSERT_FALSE(visible_floor_pixels.empty());

  auto is_visible_from_camera = [&](const glm::vec3 point) {
    const auto ray = point - camera_eye;
    const auto distance = glm::length(ray);
    const auto direction = ray / distance;
    constexpr auto kVisibilityBias = 0.02F;
    return !RayIntersectsAabb(camera_eye + direction * kVisibilityBias,
             direction, tall_box.min, tall_box.max,
             distance - 2.0F * kVisibilityBias)
      && !RayIntersectsAabb(camera_eye + direction * kVisibilityBias, direction,
        short_box.min, short_box.max, distance - 2.0F * kVisibilityBias);
  };
  auto is_shadowed_by_boxes = [&](const glm::vec3 point) {
    constexpr auto kShadowBias = 0.02F;
    const auto origin = point + sun_direction * kShadowBias;
    return RayIntersectsAabb(origin, sun_direction, tall_box.min, tall_box.max)
      || RayIntersectsAabb(origin, sun_direction, short_box.min, short_box.max);
  };

  auto shadow_probes = std::vector<ProbeSample> {};
  auto lit_probes = std::vector<ProbeSample> {};
  for (const auto& probe : visible_floor_pixels) {
    if (probe.pixel.x < 12U || probe.pixel.x > (kOutputWidth - 13U)
      || probe.pixel.y < 12U || probe.pixel.y > (kOutputHeight - 13U)) {
      continue;
    }

    if (is_shadowed_by_boxes(probe.point_ws)) {
      if (shadow_probes.size() < 4U) {
        shadow_probes.push_back(probe);
      }
    } else if (lit_probes.size() < 4U) {
      lit_probes.push_back(probe);
    }

    if (shadow_probes.size() >= 4U && lit_probes.size() >= 4U) {
      break;
    }
  }

  ASSERT_GE(shadow_probes.size(), 2U);
  ASSERT_GE(lit_probes.size(), 2U);

  auto reconstruct_world_from_depth
    = [&](const glm::uvec2 pixel, const float depth) {
        const auto ndc_x = (2.0F * (static_cast<float>(pixel.x) + 0.5F)
                             / static_cast<float>(kOutputWidth))
          - 1.0F;
        const auto ndc_y = 1.0F
          - (2.0F * (static_cast<float>(pixel.y) + 0.5F)
            / static_cast<float>(kOutputHeight));
        auto world
          = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, depth, 1.0F };
        world /= world.w;
        return glm::vec3 { world };
      };

  for (const auto& probe : { shadow_probes.front(), lit_probes.front() }) {
    const auto sampled_depth = ReadOutputTexel(depth_texture, probe.pixel.x,
      probe.pixel.y, "phase-k-two-box-floor.depth-sample");
    EXPECT_LT(sampled_depth, 1.0F)
      << "Depth pre-pass left a visible probe at clear depth for pixel ("
      << probe.pixel.x << ", " << probe.pixel.y << ")";
    const auto reconstructed
      = reconstruct_world_from_depth(probe.pixel, sampled_depth);
    EXPECT_NEAR(reconstructed.x, probe.point_ws.x, 0.2F)
      << "Reconstructed depth-sample world X mismatched at pixel ("
      << probe.pixel.x << ", " << probe.pixel.y << ")";
    EXPECT_NEAR(reconstructed.y, probe.point_ws.y, 0.2F)
      << "Reconstructed depth-sample world Y mismatched at pixel ("
      << probe.pixel.x << ", " << probe.pixel.y << ")";
    EXPECT_NEAR(reconstructed.z, probe.point_ws.z, 0.2F)
      << "Reconstructed depth-sample world Z mismatched at pixel ("
      << probe.pixel.x << ", " << probe.pixel.y << ")";
  }

  const auto output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);

  for (std::size_t i = 0; i < shadow_probes.size(); ++i) {
    const auto sample = ReadOutputTexel(output.directional_shadow_mask_texture,
      shadow_probes[i].pixel.x, shadow_probes[i].pixel.y,
      "phase-k-two-box-floor.shadow-sample");
    EXPECT_LT(sample, 0.35F)
      << "Stage 15 mask kept a shadowed floor probe lit at world point ("
      << shadow_probes[i].point_ws.x << ", " << shadow_probes[i].point_ws.y
      << ", " << shadow_probes[i].point_ws.z << ") screen pixel ("
      << shadow_probes[i].pixel.x << ", " << shadow_probes[i].pixel.y
      << ") with sample " << sample;
  }
  for (std::size_t i = 0; i < lit_probes.size(); ++i) {
    const auto sample = ReadOutputTexel(output.directional_shadow_mask_texture,
      lit_probes[i].pixel.x, lit_probes[i].pixel.y,
      "phase-k-two-box-floor.lit-sample");
    EXPECT_GT(sample, 0.65F)
      << "Stage 15 mask darkened a lit floor probe at world point ("
      << lit_probes[i].point_ws.x << ", " << lit_probes[i].point_ws.y << ", "
      << lit_probes[i].point_ws.z << ") screen pixel (" << lit_probes[i].pixel.x
      << ", " << lit_probes[i].pixel.y << ") with sample " << sample;
  }
}

#endif

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PrepareViewPublishesDirectionalClipmapPanAfterCameraTranslation)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto depth_texture
    = UploadSingleChannelTexture(0.5F, "phase-ka-live-pan.depth");

  auto scene = std::make_shared<Scene>("phase-ka-live-pan-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto run_frame_for_view = [&](const ResolvedView& view,
                              const SequenceNumber sequence) {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<Scene> { scene.get() },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto lights
      = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        oxygen::observer_ptr { &renderer->GetStagingProvider() },
        oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
    lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
      sequence, Slot { 0U });
    static_cast<void>(CollectDirectionalLight(lights, sun_node));
    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence,
      Slot { 0U });
    static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(view, sequence, Slot { 0U }), lights,
      oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
    ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);
    const auto* extracted_frame
      = vsm_renderer.GetCacheManager().GetPreviousFrame();
    CHECK_NOTNULL_F(extracted_frame,
      "directional clipmap pan test requires an extracted previous frame");
    return extracted_frame->virtual_frame;
  };

  const auto first_view = MakeResolvedView(glm::vec3 { 0.0F, 0.0F, 0.0F });
  const auto first_frame
    = run_frame_for_view(first_view, SequenceNumber { 10U });
  const auto* first_extracted_frame
    = vsm_renderer.GetCacheManager().GetPreviousFrame();
  ASSERT_NE(first_extracted_frame, nullptr);
  ASSERT_FALSE(first_frame.directional_layouts.empty());
  ASSERT_FALSE(first_frame.directional_layouts[0].page_world_size.empty());
  ASSERT_FALSE(first_frame.directional_layouts[0].page_grid_origin.empty());
  ASSERT_FALSE(first_extracted_frame->projection_records.empty());

  const auto page_world_size
    = first_frame.directional_layouts[0].page_world_size[0];
  ASSERT_GT(page_world_size, 0.0F);
  const auto first_origin
    = first_frame.directional_layouts[0].page_grid_origin[0];
  const auto first_projection_it
    = std::find_if(first_extracted_frame->projection_records.begin(),
      first_extracted_frame->projection_records.end(),
      [](const auto& projection) {
        return projection.projection.light_type
          == static_cast<std::uint32_t>(
            oxygen::renderer::vsm::VsmProjectionLightType::kDirectional);
      });
  ASSERT_NE(
    first_projection_it, first_extracted_frame->projection_records.end());

  const auto light_space_page_shift_ws
    = glm::vec3(glm::inverse(first_projection_it->projection.view_matrix)
      * glm::vec4 { page_world_size * 2.5F, 0.0F, 0.0F, 0.0F });

  const auto translated_view = MakeResolvedView(light_space_page_shift_ws);
  const auto translated_frame
    = run_frame_for_view(translated_view, SequenceNumber { 11U });
  ASSERT_FALSE(translated_frame.directional_layouts.empty());
  ASSERT_FALSE(
    translated_frame.directional_layouts[0].page_grid_origin.empty());

  EXPECT_NE(
    translated_frame.directional_layouts[0].page_grid_origin[0], first_origin);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellReusesProjectionShadowMaskAcrossConsecutiveFrames)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka6-live-shell-reuse.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka6-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto run_frame = [&](const SequenceNumber sequence) {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<Scene> { scene.get() },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto lights
      = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        oxygen::observer_ptr { &renderer->GetStagingProvider() },
        oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
    lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
      sequence, Slot { 0U });
    static_cast<void>(CollectDirectionalLight(lights, sun_node));

    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence,
      Slot { 0U });
    static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, Slot { 0U }), lights,
      oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
    ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);
  };

  run_frame(SequenceNumber { 1U });
  const auto first_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(first_output.available);
  ASSERT_NE(first_output.shadow_mask_texture, nullptr);
  const auto* first_texture = first_output.shadow_mask_texture.get();

  run_frame(SequenceNumber { 2U });
  const auto second_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(second_output.available);
  ASSERT_NE(second_output.shadow_mask_texture, nullptr);
  EXPECT_EQ(second_output.shadow_mask_texture.get(), first_texture);
  EXPECT_NE(second_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellRemainsStableAcrossManyConsecutiveFrames)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka6-live-shell-stability.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka6-stability-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const Texture* first_shadow_mask_texture = nullptr;
  for (std::uint32_t frame_index = 1U; frame_index <= 16U; ++frame_index) {
    const auto sequence = SequenceNumber { frame_index };
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<Scene> { scene.get() },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto lights
      = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        oxygen::observer_ptr { &renderer->GetStagingProvider() },
        oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
    lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
      sequence, Slot { 0U });
    static_cast<void>(CollectDirectionalLight(lights, sun_node));

    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence,
      Slot { 0U });
    static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, Slot { 0U }), lights,
      oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
    ExecutePreparedViewShell(vsm_renderer, render_context, depth_texture);

    const auto projection_output
      = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
    ASSERT_TRUE(projection_output.available);
    ASSERT_NE(projection_output.shadow_mask_texture, nullptr);
    EXPECT_NE(
      projection_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
    if (frame_index == 1U) {
      first_shadow_mask_texture = projection_output.shadow_mask_texture.get();
      ASSERT_NE(first_shadow_mask_texture, nullptr);
    } else {
      EXPECT_EQ(
        projection_output.shadow_mask_texture.get(), first_shadow_mask_texture);
    }
    EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
      VsmCacheBuildState::kIdle);
  }
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PageRequestReadbackBridgePublishesInvalidationMetadataSeedIntoCommittedCurrentFrame)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka5-seed-bridge.depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);
  {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = SequenceNumber { 1U },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);
    {
      auto depth_recorder = AcquireRecorder("phase-ka5-seed-bridge.depth-pass");
      ASSERT_NE(depth_recorder, nullptr);
      EnsureTracked(*depth_recorder, depth_texture, ResourceStates::kCommon);
      EventLoop publish_loop;
      oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
        co_await depth_pass.PrepareResources(render_context, *depth_recorder);
      });
    }
    WaitForQueueIdle();
    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(),
      SequenceNumber { 1U }, Slot { 0U });

    auto& pool_manager = vsm_renderer.GetPhysicalPagePoolManager();
    ASSERT_EQ(pool_manager.EnsureShadowPool(
                MakeShadowPoolConfig("phase-ka5-seed-shadow")),
      VsmPhysicalPoolChangeResult::kCreated);
    ASSERT_EQ(
      pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-ka5-seed-hzb")),
      oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

    const auto seam = MakeSeam(pool_manager, 1ULL, 21U, "phase-ka5-seed-frame");
    const auto current_projection_records = std::array {
      MakeLocalProjectionRecord(seam.current_frame, resolved_view)
    };
    auto metadata_seed
      = std::vector<oxygen::renderer::vsm::VsmPhysicalPageMeta>(
        seam.physical_pool.tile_capacity);
    auto physical_page_meta_seed_buffer
      = CreateMetadataSeedBuffer(metadata_seed, "phase-ka5-seed-bridge.meta");

    auto bridge_committed_requests = false;
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      bridge_committed_requests
        = co_await vsm_renderer.ExecutePageRequestReadbackBridge(render_context,
          seam, current_projection_records, physical_page_meta_seed_buffer);
    });
    WaitForQueueIdle();
    EXPECT_TRUE(bridge_committed_requests);

    const auto* committed_frame
      = vsm_renderer.GetCacheManager().GetCurrentFrame();
    ASSERT_NE(committed_frame, nullptr);
    ASSERT_TRUE(committed_frame->is_ready);
    ASSERT_NE(committed_frame->physical_page_meta_seed_buffer, nullptr);
    EXPECT_EQ(committed_frame->physical_page_meta_seed_buffer.get(),
      physical_page_meta_seed_buffer.get());
  }
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PageRequestReadbackBridgeLeavesInvalidationMetadataSeedUnsetWhenNoneIsProvided)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka5-seed-bridge-none.depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);
  {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = SequenceNumber { 1U },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);
    {
      auto depth_recorder
        = AcquireRecorder("phase-ka5-seed-bridge-none.depth-pass");
      ASSERT_NE(depth_recorder, nullptr);
      EnsureTracked(*depth_recorder, depth_texture, ResourceStates::kCommon);
      EventLoop publish_loop;
      oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
        co_await depth_pass.PrepareResources(render_context, *depth_recorder);
      });
    }
    WaitForQueueIdle();
    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(),
      SequenceNumber { 1U }, Slot { 0U });

    auto& pool_manager = vsm_renderer.GetPhysicalPagePoolManager();
    ASSERT_EQ(pool_manager.EnsureShadowPool(
                MakeShadowPoolConfig("phase-ka5-seed-none-shadow")),
      VsmPhysicalPoolChangeResult::kCreated);
    ASSERT_EQ(
      pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-ka5-seed-none-hzb")),
      oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

    const auto seam
      = MakeSeam(pool_manager, 1ULL, 21U, "phase-ka5-seed-none-frame");
    const auto current_projection_records = std::array {
      MakeLocalProjectionRecord(seam.current_frame, resolved_view)
    };

    auto bridge_committed_requests = false;
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      bridge_committed_requests
        = co_await vsm_renderer.ExecutePageRequestReadbackBridge(
          render_context, seam, current_projection_records);
    });
    WaitForQueueIdle();
    EXPECT_TRUE(bridge_committed_requests);

    const auto* committed_frame
      = vsm_renderer.GetCacheManager().GetCurrentFrame();
    ASSERT_NE(committed_frame, nullptr);
    ASSERT_TRUE(committed_frame->is_ready);
    EXPECT_EQ(committed_frame->physical_page_meta_seed_buffer, nullptr);
  }
}

} // namespace
