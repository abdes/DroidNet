//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Icons/OxygenIcons.h>
#include <Oxygen/ImGui/Styles/FontAwesome-400.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>
#include <Oxygen/Platform/ImGui/ImGuiSdl3Backend.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Vortex/Internal/ImGuiRuntime.h>
#include <Oxygen/Vortex/Passes/ImGuiOverlayPass.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::internal {

ImGuiRuntime::ImGuiRuntime(std::shared_ptr<Platform> platform,
  std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend)
  : platform_(std::move(platform))
  , graphics_backend_(std::move(graphics_backend))
{
}

ImGuiRuntime::~ImGuiRuntime() { Shutdown(); }

auto ImGuiRuntime::Initialize(std::weak_ptr<Graphics> gfx) -> bool
{
  if (!graphics_backend_) {
    return false;
  }

  try {
    graphics_backend_->Init(std::move(gfx));
    ApplyDefaultStyleAndFonts();
    initialized_ = true;
    if (window_id_ != platform::kInvalidWindowId) {
      SetWindowId(window_id_);
    }
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Vortex ImGui runtime initialization failed: {}", ex.what());
    initialized_ = false;
    return false;
  }
}

auto ImGuiRuntime::Shutdown() noexcept -> void
{
  try {
    if (platform_window_destroy_handler_token_ != 0 && platform_) {
      platform_->UnregisterWindowAboutToBeDestroyedHandler(
        platform_window_destroy_handler_token_);
      platform_window_destroy_handler_token_ = 0;
    }
  } catch (const std::exception& ex) {
    LOG_F(WARNING,
      "Vortex ImGui runtime failed to unregister destroy handler: {}",
      ex.what());
  }

  platform_backend_.reset();
  ClearOverlayFramebuffer();
  frame_started_ = false;

  if (graphics_backend_) {
    try {
      graphics_backend_->Shutdown();
    } catch (const std::exception& ex) {
      LOG_F(
        WARNING, "Vortex ImGui runtime backend shutdown failed: {}", ex.what());
    }
  }

  initialized_ = false;
}

auto ImGuiRuntime::SetWindowId(const platform::WindowIdType window_id) -> void
{
  window_id_ = window_id;
  if (!initialized_ || !graphics_backend_) {
    return;
  }

  try {
    if (platform_window_destroy_handler_token_ != 0 && platform_) {
      platform_->UnregisterWindowAboutToBeDestroyedHandler(
        platform_window_destroy_handler_token_);
      platform_window_destroy_handler_token_ = 0;
    }
  } catch (const std::exception& ex) {
    LOG_F(WARNING,
      "Vortex ImGui runtime failed to unregister destroy handler: {}",
      ex.what());
  }

  if (window_id == platform::kInvalidWindowId || !platform_) {
    platform_backend_.reset();
    return;
  }

  try {
    platform_backend_ = std::make_unique<platform::imgui::ImGuiSdl3Backend>(
      platform_, window_id, graphics_backend_->GetImGuiContext());
    platform_window_destroy_handler_token_
      = platform_->RegisterWindowAboutToBeDestroyedHandler(
        [this, window_id](const platform::WindowIdType closing_window_id) {
          if (closing_window_id == window_id) {
            SetWindowId(platform::kInvalidWindowId);
          }
        });
  } catch (const std::exception& ex) {
    LOG_F(ERROR,
      "Vortex ImGui runtime failed to bind window {}: {}", window_id, ex.what());
    platform_backend_.reset();
  }
}

auto ImGuiRuntime::OnFrameStart() -> void
{
  frame_started_ = false;
  if (!graphics_backend_ || !platform_backend_) {
    return;
  }

  platform_backend_->NewFrame();

  if (auto* ctx = GetImGuiContext()) {
    ImGui::SetCurrentContext(ctx);
    const auto& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F
      && io.DisplayFramebufferScale.x > 0.0F
      && io.DisplayFramebufferScale.y > 0.0F) {
      graphics_backend_->NewFrame();
      frame_started_ = true;
    }
  }
}

auto ImGuiRuntime::OnFrameEnd() -> void
{
  if (!frame_started_) {
    return;
  }

  if (auto* ctx = GetImGuiContext()) {
    ImGui::SetCurrentContext(ctx);
    ImGui::EndFrame();
  }

  frame_started_ = false;
}

auto ImGuiRuntime::GetImGuiContext() const noexcept -> ImGuiContext*
{
  return graphics_backend_ ? graphics_backend_->GetImGuiContext() : nullptr;
}

auto ImGuiRuntime::RenderOverlay(Renderer& renderer,
  const observer_ptr<const graphics::Framebuffer> composite_target)
  -> std::optional<OverlayComposition>
{
  if (!frame_started_ || !graphics_backend_ || composite_target == nullptr) {
    return std::nullopt;
  }

  const auto gfx = renderer.GetGraphics();
  if (!gfx) {
    return std::nullopt;
  }

  const auto& fb_desc = composite_target->GetDescriptor();
  if (fb_desc.color_attachments.empty() || !fb_desc.color_attachments[0].texture) {
    return std::nullopt;
  }

  const auto& target_desc = fb_desc.color_attachments[0].texture->GetDescriptor();
  if (!EnsureOverlayFramebuffer(
        observer_ptr { gfx.get() }, target_desc.width, target_desc.height)) {
    return std::nullopt;
  }

  auto overlay_pass = ImGuiOverlayPass { renderer };
  if (!overlay_pass.Record({
        .backend = observer_ptr { graphics_backend_.get() },
        .target = observer_ptr { overlay_framebuffer_.get() },
        .color_texture = observer_ptr { overlay_texture_.get() },
      })) {
    return std::nullopt;
  }

  return OverlayComposition {
    .texture = overlay_texture_,
    .viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(target_desc.width),
      .height = static_cast<float>(target_desc.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    },
  };
}

auto ImGuiRuntime::ApplyDefaultStyleAndFonts() -> void
{
  namespace icons = oxygen::imgui::icons;
  namespace sp = oxygen::imgui::spectrum;
  namespace styles = oxygen::imgui::styles;

  auto* ctx = GetImGuiContext();
  if (ctx == nullptr) {
    return;
  }

  ImGui::SetCurrentContext(ctx);

  constexpr float kDefaultFontSize = 16.0F;
  constexpr float kToolbarIconFontSize = 24.0F;
  ImGuiStyle& style = ImGui::GetStyle();
  sp::StyleColorsSpectrum(style);

  ImGuiIO& io = ImGui::GetIO();
  ImFont* default_font = sp::LoadFont(*io.Fonts, kDefaultFontSize);
  IM_ASSERT(default_font != nullptr);
  io.FontDefault = default_font;

  constexpr ImWchar fa_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
  ImFontConfig fa_config {};
  fa_config.MergeMode = true;
  fa_config.PixelSnapH = true;
  io.Fonts->AddFontFromMemoryCompressedTTF(
    styles::FontAwesome_compressed_data,
    static_cast<int>(styles::FontAwesome_compressed_size), kDefaultFontSize,
    &fa_config, fa_ranges);

  constexpr ImWchar oxygen_icon_ranges[] = {
    static_cast<ImWchar>(icons::kIconCameraControlsCodepoint),
    static_cast<ImWchar>(icons::kIconSettingsCodepoint),
    0,
  };

  ImFontConfig oxygen_config {};
  oxygen_config.MergeMode = false;
  oxygen_config.PixelSnapH = true;
  oxygen_config.GlyphMinAdvanceX = kToolbarIconFontSize;
  using namespace std::string_view_literals;
  constexpr auto kOxygenIconsName = "vortex-icons"sv;
  auto [_, out] = std::ranges::copy_n(kOxygenIconsName.data(),
    std::min(kOxygenIconsName.size(), std::size(oxygen_config.Name) - 1),
    oxygen_config.Name);
  *out = '\0';

  [[maybe_unused]] ImFont* oxygen_icon_font
    = io.Fonts->AddFontFromMemoryCompressedTTF(
      icons::OxygenIcons_compressed_data,
      static_cast<int>(icons::OxygenIcons_compressed_size),
      kToolbarIconFontSize, &oxygen_config, oxygen_icon_ranges);
  IM_ASSERT(oxygen_icon_font != nullptr);
}

auto ImGuiRuntime::EnsureOverlayFramebuffer(
  const observer_ptr<Graphics> gfx, const std::uint32_t width,
  const std::uint32_t height) -> bool
{
  if (gfx == nullptr || width == 0U || height == 0U) {
    return false;
  }

  if (overlay_framebuffer_ && overlay_texture_ && overlay_width_ == width
    && overlay_height_ == height) {
    return true;
  }

  ClearOverlayFramebuffer();

  graphics::TextureDesc color_desc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  color_desc.debug_name = "Vortex.ImGuiOverlay";

  overlay_texture_ = gfx->CreateTexture(color_desc);
  if (!overlay_texture_) {
    return false;
  }

  auto framebuffer_desc = graphics::FramebufferDesc {};
  framebuffer_desc.AddColorAttachment({ .texture = overlay_texture_ });
  overlay_framebuffer_ = gfx->CreateFramebuffer(framebuffer_desc);
  if (!overlay_framebuffer_) {
    overlay_texture_.reset();
    return false;
  }

  overlay_width_ = width;
  overlay_height_ = height;
  return true;
}

auto ImGuiRuntime::ClearOverlayFramebuffer() noexcept -> void
{
  overlay_framebuffer_.reset();
  overlay_texture_.reset();
  overlay_width_ = 0U;
  overlay_height_ = 0U;
}

} // namespace oxygen::vortex::internal
