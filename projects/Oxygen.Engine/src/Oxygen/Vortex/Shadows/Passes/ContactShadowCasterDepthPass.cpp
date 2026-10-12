//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <glm/ext/matrix_float4x4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Internal/MeshDepthPipeline.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Shadows/Passes/ContactShadowCasterDepthPass.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::vortex::shadows {

namespace {

  //! Mirrors ContactShadowCasterDepthPassConstants in
  //! ContactShadowCasterDepth.hlsl.
  struct alignas(packing::kShaderDataFieldAlignment)
    ContactShadowCasterDepthPassConstants {
    glm::mat4 view_matrix { 1.0F };
    glm::mat4 projection_matrix { 1.0F };
    std::uint32_t draw_frame_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t pad0 { 0U };
    std::uint32_t pad1 { 0U };
    std::uint32_t pad2 { 0U };
  };

  static_assert(sizeof(ContactShadowCasterDepthPassConstants)
    == 144U); // NOLINT(*-magic-numbers)
  static_assert(
    offsetof(ContactShadowCasterDepthPassConstants, view_matrix) == 0U);
  static_assert(offsetof(ContactShadowCasterDepthPassConstants,
                  projection_matrix)
    == 64U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ContactShadowCasterDepthPassConstants, draw_frame_slot)
    == 128U); // NOLINT(*-magic-numbers)
  constexpr std::uint32_t kPassConstantsStride
    = sizeof(ContactShadowCasterDepthPassConstants);

} // namespace

ContactShadowCasterDepthPass::ContactShadowCasterDepthPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetLightingStagingProvider(), kPassConstantsStride,
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Vortex.ContactShadowCasterDepth.PassConstants")
  , mesh_processor_(std::make_unique<DepthPrepassMeshProcessor>(renderer))
  , draw_cull_(std::make_unique<occlusion::internal::DrawCullPass>(
      renderer, "Vortex.Stage8.ContactShadowCasterDepth.DrawCull"))
  , list_builder_(std::make_unique<occlusion::internal::IndirectListBuilder>(
      renderer, "Vortex.Stage8.ContactShadowCasterDepth.Lists"))
{
}

ContactShadowCasterDepthPass::~ContactShadowCasterDepthPass() = default;

void ContactShadowCasterDepthPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot)
{
  sequence_ = sequence;
  slot_ = slot;
  pass_constants_buffer_.OnFrameStart(sequence, slot);
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
    || !input.draw_frame_slot.IsValid()) {
    return {};
  }
  const auto& view = *input.resolved_view;
  const auto viewport = view.Viewport();
  const auto width
    = std::ceil(static_cast<double>(viewport.top_left_x) + viewport.width);
  const auto height
    = std::ceil(static_cast<double>(viewport.top_left_y) + viewport.height);
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0
    || height <= 0.0 || viewport.top_left_x < 0.0F || viewport.top_left_y < 0.0F
    || viewport.width <= 0.0F || viewport.height <= 0.0F
    || width > std::numeric_limits<std::int32_t>::max()
    || height > std::numeric_limits<std::int32_t>::max()) {
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
  auto& registry = gfx->GetResourceRegistry();
  auto srv = registry.FindShaderVisibleIndex(*texture, srv_desc);
  if (!srv) {
    auto allocation = descriptors.AllocateBindless(
      bindless::generated::kTexturesDomain, srv_desc.view_type);
    if (!allocation.IsValid()) {
      return {};
    }
    srv = descriptors.GetShaderVisibleIndex(allocation);
    const auto resource_view
      = registry.RegisterView(*texture, std::move(allocation), srv_desc);
    if (!resource_view->IsValid()) {
      return {};
    }
  }
  graphics::FramebufferDesc framebuffer_desc;
  framebuffer_desc.SetDepthAttachment(
    { .texture = texture, .format = desc.format, .is_read_only = false });
  auto pass_constants = pass_constants_buffer_.Allocate(1U);
  if (!pass_constants.has_value() || !pass_constants->IsValid(sequence_)
    || !pass_constants->TryWriteObject(ContactShadowCasterDepthPassConstants {
      .view_matrix = view.ViewMatrix(),
      .projection_matrix = view.ProjectionMatrix(),
      .draw_frame_slot = input.draw_frame_slot.get(),
    })) {
    LOG_F(ERROR,
      "ContactShadowCasterDepthPass: failed to publish pass constants for view "
      "{}",
      input.view_id.get());
    return {};
  }
  const auto pass_constants_srv = pass_constants->srv;
  const auto framebuffer = gfx->CreateFramebuffer(framebuffer_desc);
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "ContactShadowCasterDepth");
  if (!framebuffer || !recorder) {
    return {};
  }
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);
  mesh_processor_->BuildDrawCommands(*input.prepared_scene, &view, true, true);
  {
    graphics::GpuEventScope scope(*recorder,
      "Vortex.Stage8.ContactShadowCasterDepth",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    if (!recorder->AdoptKnownResourceState(*texture)) {
      recorder->BeginTrackingResourceState(*texture, desc.initial_state, false);
    }
    const auto clipped = vortex::internal::ResolveClampedViewportState(
      viewport, view.Scissor(), desc.width, desc.height);
    const auto visibility = draw_cull_->RunPhase1(*recorder,
      occlusion::internal::DrawCullInputs {
        .frame_sequence = sequence_,
        .frame_slot = slot_,
        .prepared_frame = input.prepared_scene,
        .view_projection = view.ProjectionMatrix() * view.ViewMatrix(),
        .viewport = clipped.viewport,
        .scissors = clipped.scissors,
      });
    const auto draw_list = list_builder_->Build(*recorder, sequence_, slot_,
      occlusion::internal::MakeIndirectDrawCandidates(
        input.prepared_scene->GetDrawMetadata(),
        mesh_processor_->GetDrawCommands()),
      visibility, DrawVisibilityPredicate::Drawn());
    recorder->RequireResourceState(
      *texture, graphics::ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    recorder->ClearFramebuffer(
      *framebuffer, std::nullopt, clear_depth, std::nullopt);
    recorder->BindFrameBuffer(*framebuffer);
    recorder->SetViewport(clipped.viewport);
    recorder->SetScissors(clipped.scissors);
    using bindless::generated::d3d12::RootParam;
    const auto root = static_cast<std::uint32_t>(RootParam::kRootConstants);
    occlusion::internal::IndirectListBuilder::Draw(*recorder, draw_list,
      [&](const vortex::internal::MeshRasterState& raster) -> void {
        recorder->SetPipelineState(
          vortex::internal::BuildContactShadowCasterDepthPipeline(
            desc, raster, view.ReverseZ()));
        recorder->SetGraphicsRoot32BitConstant(
          root, pass_constants_srv.get(), 1U);
      });
    recorder->RequireResourceStateFinal(
      *texture, graphics::ResourceStates::kShaderResource);
  }
  if (!recorder.Submit()) {
    return {};
  }
  bindings.contact_depth_srv = *srv;
  bindings.contact_enabled = 1U;
  bindings.contact_content_origin_px
    = { viewport.top_left_x, viewport.top_left_y };
  bindings.contact_content_extent_px = { viewport.width, viewport.height };
  bindings.contact_texture_extent_px = { desc.width, desc.height };
  return texture;
}

} // namespace oxygen::vortex::shadows
