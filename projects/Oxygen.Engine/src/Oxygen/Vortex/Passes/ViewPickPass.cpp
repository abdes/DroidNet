//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/Passes/ViewPickPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kPickFormat = Format::kRG32UInt;
  constexpr auto kPickDepthFormat = Format::kDepth32;

  auto BuildPickPipelineDesc(const bool alpha_test, const bool reverse_z)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = RenderPass::BuildRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    if (alpha_test) {
      defines.push_back({ .name = "ALPHA_TEST", .value = "1" });
    }
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
        .entry_point = "DepthPrepassVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Editor/ViewPick.hlsl",
        .entry_point = "VortexViewPickPS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      // Both faces: a thin or open surface is pickable from behind.
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_func = reverse_z ? graphics::CompareOp::kGreater
                                : graphics::CompareOp::kLess,
        .stencil_enable = false,
      })
      .SetBlendState({ graphics::BlendTargetDesc {} })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = { kPickFormat },
        .depth_stencil_format = kPickDepthFormat,
        .sample_count = 1U,
        .sample_quality = 0U,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(alpha_test ? "Vortex.ViewPick.Masked" : "Vortex.ViewPick")
      .Build();
  }

  auto CreatePickTexture(Graphics& gfx, const ViewPickRect& rect,
    const Format format, const bool depth) -> std::shared_ptr<graphics::Texture>
  {
    auto desc = graphics::TextureDesc {};
    desc.width = rect.width;
    desc.height = rect.height;
    desc.format = format;
    desc.texture_type = TextureType::kTexture2D;
    desc.is_render_target = true;
    desc.is_shader_resource = false;
    desc.use_clear_value = false;
    desc.initial_state = depth ? graphics::ResourceStates::kDepthWrite
                               : graphics::ResourceStates::kRenderTarget;
    desc.debug_name = depth ? "Vortex.ViewPick.Depth" : "Vortex.ViewPick.Ids";
    return gfx.CreateTexture(desc);
  }

  auto IsNearer(const float depth, const float than, const bool reverse_z)
    -> bool
  {
    return reverse_z ? depth > than : depth < than;
  }

} // namespace

ViewPickPass::ViewPickPass(Renderer& renderer)
  : renderer_(renderer)
{
}

ViewPickPass::~ViewPickPass() = default;

auto ViewPickPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, std::shared_ptr<ViewPickRequest> request)
  -> void
{
  if (request == nullptr) {
    return;
  }
  const auto fail = [&request]() -> void {
    request->Complete(ViewPickResult {
      .status = ViewPickResult::Status::kFailed,
      .hits = {},
      .world_position = std::nullopt,
    });
  };

  auto gfx = renderer_.GetGraphics();
  const auto readbacks = gfx ? gfx->GetReadbackManager() : nullptr;
  const auto* resolved_view = ctx.current_view.resolved_view.get();
  if (gfx == nullptr || readbacks == nullptr || resolved_view == nullptr
    || ctx.view_constants == nullptr) {
    fail();
    return;
  }

  const auto& rect = request->Rect();
  const auto reverse_z = resolved_view->ReverseZ();
  auto ids = CreatePickTexture(*gfx, rect, kPickFormat, false);
  auto depth = CreatePickTexture(*gfx, rect, kPickDepthFormat, true);
  if (ids == nullptr || depth == nullptr) {
    fail();
    return;
  }
  auto framebuffer = gfx->CreateFramebuffer(
    graphics::FramebufferDesc {}.AddColorAttachment(ids).SetDepthAttachment(
      depth));
  if (framebuffer == nullptr) {
    fail();
    return;
  }

  auto sources = std::vector<PreparedSceneFrame::DrawSource> {};
  {
    graphics::GpuEventScope pass_scope(recorder, "Vortex.ViewPick",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    recorder.BeginTrackingResourceState(
      *ids, graphics::ResourceStates::kRenderTarget, false);
    recorder.BeginTrackingResourceState(
      *depth, graphics::ResourceStates::kDepthWrite, false);
    recorder.BindFrameBuffer(*framebuffer);
    recorder.ClearFramebuffer(*framebuffer,
      std::vector<std::optional<graphics::Color>> {
        graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F } },
      reverse_z ? 0.0F : 1.0F);

    // The view's projection, with the pick rectangle moved to the origin.
    auto viewport = resolved_view->Viewport();
    viewport.top_left_x -= static_cast<float>(rect.x);
    viewport.top_left_y -= static_cast<float>(rect.y);
    recorder.SetViewport(viewport);
    recorder.SetScissors(Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(rect.width),
      .bottom = static_cast<std::int32_t>(rect.height),
    });

    const auto* prepared_frame = ctx.current_view.prepared_frame.get();
    if (prepared_frame != nullptr
      && prepared_frame->draw_sources.size()
        == prepared_frame->GetDrawMetadata().size()) {
      const auto metadata = prepared_frame->GetDrawMetadata();
      sources.assign(prepared_frame->draw_sources.begin(),
        prepared_frame->draw_sources.end());
      const auto root_constants
        = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
      const auto view_constants
        = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
      const auto accept_mask = PassMask {
        PassMaskBit::kOpaque,
        PassMaskBit::kMasked,
        PassMaskBit::kTransparent,
      };
      auto current_alpha_test = std::optional<bool> {};
      for (const auto [draw, draw_index] :
        AcceptedDrawView(*prepared_frame, accept_mask)) {
        if (!draw->flags.IsSet(PassMaskBit::kMainViewVisible)) {
          continue;
        }
        const auto alpha_test
          = internal::ResolveMeshRasterState(metadata, draw_index).alpha_test;
        if (current_alpha_test != alpha_test) {
          recorder.SetPipelineState(
            BuildPickPipelineDesc(alpha_test, reverse_z));
          recorder.SetGraphicsRootConstantBufferView(
            view_constants, ctx.view_constants->GetGPUVirtualAddress());
          current_alpha_test = alpha_test;
        }
        recorder.SetGraphicsRoot32BitConstant(root_constants, 0U, 1U);
        recorder.Draw(
          draw->is_indexed != 0U ? draw->index_count : draw->vertex_count,
          (std::max)(draw->instance_count, 1U), 0U, draw_index);
      }
    }
  }

  auto readback = readbacks->CreateTextureReadback("Vortex.ViewPick");
  const auto queued
    = readback != nullptr && readback->EnqueueCopy(recorder, *ids).has_value();
  gfx->RegisterDeferredRelease(std::move(framebuffer));
  gfx->RegisterDeferredRelease(std::move(ids));
  gfx->RegisterDeferredRelease(std::move(depth));
  if (!queued) {
    LOG_F(WARNING, "ViewPickPass: pick readback could not be queued");
    fail();
    return;
  }

  const auto projection = resolved_view->ProjectionMatrix();
  const auto view = resolved_view->ViewMatrix();
  pending_.push_back(PendingPick {
    .request = std::move(request),
    .readback = std::move(readback),
    .sources = std::move(sources),
    .geometry = PickGeometry {
      .rect = rect,
      .viewport = resolved_view->Viewport(),
      .inv_view_proj = glm::inverse(projection * view),
      .reverse_z = reverse_z,
    },
  });
}

auto ViewPickPass::Poll() -> void
{
  auto completed = std::vector<
    std::pair<std::shared_ptr<ViewPickRequest>, ViewPickResult>> {};
  std::erase_if(pending_, [&completed](PendingPick& pick) -> bool {
    auto mapped = pick.readback->TryMap();
    if (!mapped.has_value()) {
      if (mapped.error() == graphics::ReadbackError::kNotReady) {
        return false;
      }
      completed.emplace_back(std::move(pick.request),
        ViewPickResult {
          .status = ViewPickResult::Status::kFailed,
          .hits = {},
          .world_position = std::nullopt,
        });
      return true;
    }
    const auto& layout = mapped->Layout();
    const auto row_stride = layout.row_pitch.get() / sizeof(std::uint32_t);
    // Copy out of the mapping: it may not be aligned for typed access.
    auto texels = std::vector<std::uint32_t>(
      row_stride * static_cast<std::size_t>(layout.height));
    std::memcpy(
      texels.data(), mapped->Data(), texels.size() * sizeof(std::uint32_t));
    completed.emplace_back(std::move(pick.request),
      ResolveHits(
        PickImage {
          .texels = texels,
          .width = layout.width,
          .height = layout.height,
          .row_stride = row_stride,
        },
        pick.sources, pick.geometry));
    return true;
  });
  // Completions run after the bookkeeping, outside any mapping.
  for (auto& [request, result] : completed) {
    request->Complete(std::move(result));
  }
}

auto ViewPickPass::ResolveHits(const PickImage& image,
  const std::span<const PreparedSceneFrame::DrawSource> sources,
  const PickGeometry& geometry) -> ViewPickResult
{
  struct NodeHit {
    ViewPickHit hit {};
    std::uint32_t x { 0U };
    std::uint32_t y { 0U };
  };
  auto by_node = std::unordered_map<scene::NodeHandle, NodeHit> {};
  const auto center_x = (static_cast<float>(image.width) - 1.0F) * 0.5F;
  const auto center_y = (static_cast<float>(image.height) - 1.0F) * 0.5F;
  for (std::uint32_t y = 0U; y < image.height; ++y) {
    for (std::uint32_t x = 0U; x < image.width; ++x) {
      const auto offset = static_cast<std::size_t>(y) * image.row_stride
        + static_cast<std::size_t>(x) * 2U;
      if (offset + 1U >= image.texels.size()) {
        continue;
      }
      const auto id = image.texels[offset];
      if (id == 0U || id > sources.size()) {
        continue;
      }
      const auto& source = sources[id - 1U];
      const auto depth = std::bit_cast<float>(image.texels[offset + 1U]);
      const auto distance = std::hypot(
        static_cast<float>(x) - center_x, static_cast<float>(y) - center_y);
      auto [it, inserted] = by_node.try_emplace(source.node);
      auto& entry = it->second;
      if (inserted) {
        entry.hit = ViewPickHit {
          .node = source.node,
          .depth = depth,
          .submesh_index = source.submesh_index,
          .center_distance = distance,
        };
        entry.x = x;
        entry.y = y;
        continue;
      }
      if (IsNearer(depth, entry.hit.depth, geometry.reverse_z)) {
        entry.hit.depth = depth;
      }
      if (distance < entry.hit.center_distance) {
        entry.hit.center_distance = distance;
        entry.hit.submesh_index = source.submesh_index;
        entry.x = x;
        entry.y = y;
      }
    }
  }

  auto ordered = std::vector<NodeHit> {};
  ordered.reserve(by_node.size());
  for (auto& [_, entry] : by_node) {
    ordered.push_back(entry);
  }
  std::ranges::sort(ordered, [&geometry](const NodeHit& a, const NodeHit& b) {
    if (a.hit.center_distance != b.hit.center_distance) {
      return a.hit.center_distance < b.hit.center_distance;
    }
    return IsNearer(a.hit.depth, b.hit.depth, geometry.reverse_z);
  });

  auto result = ViewPickResult {
    .status = ViewPickResult::Status::kCompleted,
    .hits = {},
    .world_position = std::nullopt,
  };
  result.hits.reserve(ordered.size());
  for (const auto& entry : ordered) {
    result.hits.push_back(entry.hit);
  }
  if (!ordered.empty() && geometry.viewport.width > 0.0F
    && geometry.viewport.height > 0.0F) {
    // Depth of the first hit's chosen pixel, not its nearest one.
    const auto& first = ordered.front();
    const auto offset = static_cast<std::size_t>(first.y) * image.row_stride
      + static_cast<std::size_t>(first.x) * 2U;
    const auto depth = std::bit_cast<float>(image.texels[offset + 1U]);
    const auto pixel_x = static_cast<float>(geometry.rect.x + first.x) + 0.5F;
    const auto pixel_y = static_cast<float>(geometry.rect.y + first.y) + 0.5F;
    const auto ndc_x = (pixel_x - geometry.viewport.top_left_x)
        / geometry.viewport.width * 2.0F
      - 1.0F;
    const auto ndc_y = 1.0F
      - (pixel_y - geometry.viewport.top_left_y) / geometry.viewport.height
        * 2.0F;
    const auto world
      = geometry.inv_view_proj * glm::vec4(ndc_x, ndc_y, depth, 1.0F);
    if (std::isfinite(world.w) && std::abs(world.w) > 1.0e-12F) {
      result.world_position = glm::vec3(world) / world.w;
    }
  }
  return result;
}

} // namespace oxygen::vortex
