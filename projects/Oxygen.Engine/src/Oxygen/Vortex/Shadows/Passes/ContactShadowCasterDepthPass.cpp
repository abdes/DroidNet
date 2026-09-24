//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Internal/MeshDepthPipeline.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Passes/ContactShadowCasterDepthPass.h>

namespace oxygen::vortex::shadows {

ContactShadowCasterDepthPass::ContactShadowCasterDepthPass(Renderer& renderer)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<DepthPrepassMeshProcessor>(renderer))
{
}

ContactShadowCasterDepthPass::~ContactShadowCasterDepthPass() = default;

void ContactShadowCasterDepthPass::OnFrameStart(
  const frame::SequenceNumber sequence)
{
  sequence_ = sequence;
  if (textures_) {
    textures_->OnFrameStart(sequence);
  }
}

void ContactShadowCasterDepthPass::RemoveView(const ViewId view_id)
{
  if (textures_) {
    textures_->RemoveView(view_id);
  }
}

auto ContactShadowCasterDepthPass::Record(const PreparedViewShadowInput& input,
  ShadowFrameBindings& bindings) -> std::shared_ptr<graphics::Texture>
{
  const auto gfx = renderer_.GetGraphics();
  if (!gfx || !input.resolved_view || !input.prepared_scene
    || !input.view_constants) {
    return {};
  }
  const auto& view = *input.resolved_view;
  const auto viewport = view.Viewport();
  const auto width = std::ceil(double(viewport.top_left_x) + viewport.width);
  const auto height = std::ceil(double(viewport.top_left_y) + viewport.height);
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0
    || height <= 0.0 || viewport.top_left_x < 0.0F
    || viewport.top_left_y < 0.0F || viewport.width <= 0.0F
    || viewport.height <= 0.0F
    || width > (std::numeric_limits<std::int32_t>::max)()
    || height > (std::numeric_limits<std::int32_t>::max)()) {
    return {};
  }
  if (!textures_) {
    textures_ = std::make_unique<vortex::internal::RetainedTexturePool>(gfx);
    textures_->OnFrameStart(sequence_);
  }
  graphics::TextureDesc desc;
  desc.width = static_cast<std::uint32_t>(width);
  desc.height = static_cast<std::uint32_t>(height);
  desc.format = Format::kDepth32;
  desc.debug_name = "Vortex.ContactShadowCasterDepth";
  desc.is_shader_resource = desc.is_render_target = desc.is_typeless = true;
  desc.use_clear_value = true;
  const auto clear_depth = view.ReverseZ() ? 0.0F : 1.0F;
  desc.clear_value = graphics::Color { clear_depth, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;
  desc.allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() };
  auto texture = textures_->Acquire(input.view_id, desc, true);
  if (!texture) {
    return {};
  }

  const auto srv_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = Format::kR32Float,
    .dimension = TextureType::kTexture2D,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };
  auto& descriptors = gfx->GetDescriptorAllocator();
  auto allocation = descriptors.AllocateRaw(srv_desc.view_type, srv_desc.visibility);
  if (!allocation.IsValid()) {
    return {};
  }
  const auto srv = descriptors.GetShaderVisibleIndex(allocation);
  const auto resource_view = gfx->GetResourceRegistry().RegisterView(
    *texture, std::move(allocation), srv_desc);
  if (!resource_view->IsValid()) {
    return {};
  }
  graphics::FramebufferDesc framebuffer_desc;
  framebuffer_desc.SetDepthAttachment({ .texture = texture,
    .format = desc.format, .is_read_only = false });
  const auto framebuffer = gfx->CreateFramebuffer(framebuffer_desc);
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics), "ContactShadowCasterDepth");
  if (!framebuffer || !recorder) {
    return {};
  }
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);
  mesh_processor_->BuildDrawCommands(*input.prepared_scene, &view, true, true);
  {
    graphics::GpuEventScope scope(*recorder, "Vortex.Stage8.ContactShadowCasterDepth",
      profiling::ProfileGranularity::kTelemetry, profiling::ProfileCategory::kPass);
    if (!recorder->AdoptKnownResourceState(*texture)) {
      recorder->BeginTrackingResourceState(*texture, desc.initial_state, false);
    }
    recorder->RequireResourceState(*texture, graphics::ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    recorder->ClearFramebuffer(*framebuffer, std::nullopt, clear_depth, std::nullopt);
    recorder->BindFrameBuffer(*framebuffer);
    const auto clipped = vortex::internal::ResolveClampedViewportState(
      viewport, view.Scissor(), desc.width, desc.height);
    recorder->SetViewport(clipped.viewport);
    recorder->SetScissors(clipped.scissors);
    using RootParam = bindless::generated::d3d12::RootParam;
    const auto root = static_cast<std::uint32_t>(RootParam::kRootConstants);
    auto current = std::optional<vortex::internal::MeshRasterState> {};
    for (const auto& draw : mesh_processor_->GetDrawCommands()) {
      const auto raster = vortex::internal::ResolveMeshRasterState(
        input.prepared_scene->GetDrawMetadata(), draw.draw_index);
      if (!current || *current != raster) {
        recorder->SetPipelineState(vortex::internal::BuildMeshDepthPipeline(
          desc, Format::kUnknown, raster, view.ReverseZ()));
        recorder->SetGraphicsRootConstantBufferView(
          static_cast<std::uint32_t>(RootParam::kViewConstants),
          input.view_constants->GetGPUVirtualAddress());
        recorder->SetGraphicsRoot32BitConstant(root, kInvalidShaderVisibleIndex.get(), 1U);
        current = raster;
      }
      recorder->SetGraphicsRoot32BitConstant(root, draw.draw_index, 0U);
      recorder->Draw(draw.index_count, draw.instance_count, 0U, draw.start_instance);
    }
    recorder->RequireResourceStateFinal(*texture, graphics::ResourceStates::kShaderResource);
  }
  if (!recorder.Submit()) {
    return {};
  }
  bindings.contact_depth_srv = srv;
  bindings.contact_enabled = 1U;
  bindings.contact_content_origin_px = { viewport.top_left_x, viewport.top_left_y };
  bindings.contact_content_extent_px = { viewport.width, viewport.height };
  bindings.contact_texture_extent_px = { desc.width, desc.height };
  return texture;
}

} // namespace oxygen::vortex::shadows
