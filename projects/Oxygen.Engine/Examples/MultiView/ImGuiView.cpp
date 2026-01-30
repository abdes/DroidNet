//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>

#include "MultiView/ImGuiView.h"
#include "MultiView/OffscreenCompositor.h"

namespace oxygen::examples::multiview {

ImGuiView::ImGuiView()
  : DemoView(ViewConfig {
      .name = "ImGuiView",
      .purpose = "Overlay",
      .clear_color = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F }, // Transparent
      .wireframe = false,
    })
{
}

void ImGuiView::Initialize(scene::Scene& /*scene*/)
{
  // ImGui view doesn't need to initialize scene objects
}

void ImGuiView::OnSceneMutation()
{

  // 1. Ensure Render Targets (Resize if needed)
  EnsureImGuiRenderTargets();
  if (!FramebufferRef() || !ColorTextureRef()) {
    SetViewReady(false);
    return;
  }
  SetViewReady(true);

  // 2. Update View in FrameContext
  UpdateViewForCurrentSurface();
}

auto ImGuiView::OnPreRender(engine::Renderer& /*renderer*/) -> co::Co<>
{
  if (!FramebufferRef() || !ColorTextureRef()) {
    SetViewReady(false);
    co_return;
  }
  SetViewReady(true);

  co_return;
}

void ImGuiView::RegisterViewForRendering(engine::Renderer& renderer)
{
  if (GetViewId().get() == 0) {
    LOG_F(WARNING, "[ImGuiView] ViewId not assigned; skipping renderer hooks");
    return;
  }

  // We provide a dummy resolver since ImGui doesn't render scene objects.
  renderer.RegisterView(
    GetViewId(),
    [](const engine::ViewContext&) {
      return ResolvedView { ResolvedView::Params {} };
    },
    [this](ViewId /*id*/, const engine::RenderContext& ctx,
      graphics::CommandRecorder& recorder) -> co::Co<> {
      co_await RenderFrame(ctx, recorder);
    });
}

//! Updates the view context to the current surface size.
void ImGuiView::UpdateViewForCurrentSurface()
{
  const auto width = static_cast<float>(GetSurface().Width());
  const auto height = static_cast<float>(GetSurface().Height());

  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  const Scissors scissor { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };

  AddViewToFrameContext(viewport, scissor);
}

auto ImGuiView::RenderFrame(const engine::RenderContext& /*render_ctx*/,
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!IsViewReady() || !FramebufferRef() || !ColorTextureRef()) {
    co_return;
  }

  try {
    if (!imgui_module_) {
      LOG_F(WARNING, "[ImGuiView] ImGui module not set");
      co_return;
    }

    if (!imgui_module_->IsWitinFrameScope()) {
      co_return;
    }

    if (auto* imgui_context = imgui_module_->GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }

    const auto imgui_pass = imgui_module_->GetRenderPass();
    if (!imgui_pass) {
      co_return;
    }

    // Transition to RenderTarget
    recorder.RequireResourceState(
      *ColorTextureRef(), graphics::ResourceStates::kRenderTarget);
    recorder.FlushBarriers();

    // Bind and Clear
    recorder.BindFrameBuffer(*FramebufferRef());
    recorder.ClearFramebuffer(*FramebufferRef());

    // Render ImGui
    co_await imgui_pass->Render(recorder);

    // Transition back to Common/ShaderResource for compositing
    // (This is usually handled by the next phase or explicit transitions)
    // But for safety within this recorder context if used immediately:
    // However, DemoView usually expects the framework to handle state
    // transitions if registered properly, but here we are manual.
    // We leave it in RenderTarget state, or transition?
    // MainModule::RenderImGuiOverlay transitioned it back to kCommon.
    // Let's do that to be safe and match previous behavior.
    recorder.RequireResourceState(
      *ColorTextureRef(), graphics::ResourceStates::kCommon);
    recorder.FlushBarriers();

  } catch (const std::exception& e) {
    LOG_F(ERROR, "[ImGuiView] RenderFrame failed: {}", e.what());
  }

  co_return;
}

void ImGuiView::Composite(
  graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
{
  if (!IsViewReady() || !ColorTextureRef()) {
    return;
  }

  // Use OffscreenCompositor to blend the UI over the backbuffer
  // We composite to the full screen
  const auto& src_desc = ColorTextureRef()->GetDescriptor();
  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(src_desc.width),
    .height = static_cast<float>(src_desc.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  OffscreenCompositor compositor;
  // Use Blend mode for UI (alpha transparency)
  compositor.CompositeToRegion(
    recorder, *ColorTextureRef(), backbuffer, viewport);
}

void ImGuiView::OnReleaseResources()
{
  // Base class handles releasing FramebufferRef and ColorTextureRef via
  // ResetConfiguration if we used RendererRef, but we set them manually.
  // Actually DemoView doesn't automatically clear the refs in its
  // OnReleaseResources unless they are standard. PipView clears them. We should
  // clear them too.
  ColorTextureRef() = nullptr;
  FramebufferRef() = nullptr;
  last_surface_width_ = 0;
  last_surface_height_ = 0;

  DemoView::OnReleaseResources();
}

void ImGuiView::EnsureImGuiRenderTargets()
{
  auto& gfx = GetGraphics();
  const auto width = GetSurface().Width();
  const auto height = GetSurface().Height();

  if (width == last_surface_width_ && height == last_surface_height_
    && FramebufferRef() && ColorTextureRef()) {
    return;
  }

  // Check if we need recreation
  bool recreate = !FramebufferRef() || !ColorTextureRef();
  if (!recreate && ColorTextureRef()) {
    const auto& desc = ColorTextureRef()->GetDescriptor();
    if (desc.width != width || desc.height != height) {
      recreate = true;
    }
  }

  if (!recreate) {
    return;
  }

  LOG_F(INFO, "[ImGuiView] Creating render target ({}x{})", width, height);

  ColorTextureRef() = nullptr;
  FramebufferRef() = nullptr;

  graphics::TextureDesc color_desc;
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.debug_name = "ImGuiView_Color";
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.mip_levels = 1;
  color_desc.array_size = 1;
  color_desc.sample_count = 1;
  color_desc.depth = 1;
  color_desc.use_clear_value = true;
  color_desc.clear_value = Config().clear_color;
  color_desc.initial_state = graphics::ResourceStates::kCommon;

  ColorTextureRef() = gfx.CreateTexture(color_desc);

  graphics::FramebufferDesc fb_desc;
  fb_desc.AddColorAttachment({
    .texture = ColorTextureRef(),
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .format = ColorTextureRef()->GetDescriptor().format,
  });

  FramebufferRef() = gfx.CreateFramebuffer(fb_desc);

  last_surface_width_ = width;
  last_surface_height_ = height;
}

} // namespace oxygen::examples::multiview
