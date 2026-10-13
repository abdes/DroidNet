//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Icons/OxygenIcons.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/ImGui/PlatformBackend.h>
#include <Oxygen/ImGui/Styles/FontAwesome-400.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::imgui {

ImGuiModule::ImGuiModule() = default;

ImGuiModule::~ImGuiModule() { OnShutdown(); }

auto ImGuiModule::OnAttached(const observer_ptr<IAsyncEngine> engine) noexcept
  -> bool
{
  if (engine == nullptr) {
    return false;
  }

  try {
    const auto gfx = engine->GetGraphics().lock();
    platform_ = engine->GetPlatformShared();
    if (!gfx || !platform_) {
      LOG_F(ERROR, "ImGui module needs graphics and a platform");
      return false;
    }

    graphics_backend_ = gfx->CreateImGuiGraphicsBackend();
    if (!graphics_backend_) {
      LOG_F(
        ERROR, "Graphics backend '{}' has no ImGui renderer", gfx->GetName());
      return false;
    }

    graphics_backend_->Init(gfx);
    ApplyDefaultStyleAndFonts();
    if (window_id_ != platform::kInvalidWindowId) {
      SetWindowId(window_id_);
    }
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "ImGui module initialization failed: {}", ex.what());
    graphics_backend_.reset();
    return false;
  }
}

auto ImGuiModule::OnShutdown() noexcept -> void
{
  UnregisterWindowHandler();
  platform_backend_.reset();
  frame_started_ = false;

  if (graphics_backend_) {
    try {
      graphics_backend_->Shutdown();
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "ImGui graphics backend shutdown failed: {}", ex.what());
    }
    graphics_backend_.reset();
  }
  platform_.reset();
}

auto ImGuiModule::OnFrameStart(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
  frame_started_ = false;
  if (!graphics_backend_ || !platform_backend_) {
    return;
  }

  platform_backend_->NewFrame();

  auto* const ctx = GetImGuiContext();
  if (ctx == nullptr) {
    return;
  }
  ImGui::SetCurrentContext(ctx);
  // A minimized window reports no display area; ImGui asserts on NewFrame.
  const auto& io = ImGui::GetIO();
  if (io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F
    && io.DisplayFramebufferScale.x > 0.0F
    && io.DisplayFramebufferScale.y > 0.0F) {
    graphics_backend_->NewFrame();
    frame_started_ = true;
  }
}

auto ImGuiModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
  if (!frame_started_) {
    return;
  }

  if (auto* const ctx = GetImGuiContext(); ctx != nullptr) {
    ImGui::SetCurrentContext(ctx);
    ImGui::EndFrame();
  }
  frame_started_ = false;
}

auto ImGuiModule::SetWindowId(const platform::WindowIdType window_id) -> void
{
  window_id_ = window_id;
  if (!graphics_backend_) {
    // Bound on attach.
    return;
  }

  UnregisterWindowHandler();
  platform_backend_.reset();
  if (window_id == platform::kInvalidWindowId || !platform_) {
    return;
  }

  try {
    platform_backend_ = std::make_unique<PlatformBackend>(
      platform_, window_id, graphics_backend_->GetImGuiContext());
    window_destroy_handler_token_
      = platform_->RegisterWindowAboutToBeDestroyedHandler(
        [this, window_id](
          const platform::WindowIdType closing_window_id) -> void {
          if (closing_window_id == window_id) {
            SetWindowId(platform::kInvalidWindowId);
          }
        });
  } catch (const std::exception& ex) {
    LOG_F(
      ERROR, "ImGui module failed to bind window {}: {}", window_id, ex.what());
    platform_backend_.reset();
  }
}

auto ImGuiModule::GetImGuiContext() const noexcept -> ImGuiContext*
{
  return graphics_backend_ ? graphics_backend_->GetImGuiContext() : nullptr;
}

auto ImGuiModule::Render(graphics::CommandRecorder& recorder) -> void
{
  if (!frame_started_ || !graphics_backend_) {
    return;
  }
  graphics_backend_->Render(recorder);
}

auto ImGuiModule::UnregisterWindowHandler() noexcept -> void
{
  if (window_destroy_handler_token_ == 0U || !platform_) {
    return;
  }
  try {
    platform_->UnregisterWindowAboutToBeDestroyedHandler(
      window_destroy_handler_token_);
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "ImGui module failed to unregister window handler: {}",
      ex.what());
  }
  window_destroy_handler_token_ = 0U;
}

auto ImGuiModule::ApplyDefaultStyleAndFonts() const -> void
{
  namespace icons = oxygen::imgui::icons;
  namespace sp = oxygen::imgui::spectrum;
  namespace styles = oxygen::imgui::styles;

  auto* const ctx = GetImGuiContext();
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

  constexpr auto fa_ranges
    = std::array<ImWchar, 3> { ICON_MIN_FA, ICON_MAX_FA, 0 };
  ImFontConfig fa_config {};
  fa_config.MergeMode = true;
  fa_config.PixelSnapH = true;
  io.Fonts->AddFontFromMemoryCompressedTTF(
    static_cast<const void*>(styles::FontAwesome_compressed_data),
    static_cast<int>(styles::FontAwesome_compressed_size), kDefaultFontSize,
    &fa_config, fa_ranges.data());

  constexpr auto oxygen_icon_ranges = std::array<ImWchar, 3> {
    static_cast<ImWchar>(icons::kIconCameraControlsCodepoint),
    static_cast<ImWchar>(icons::kIconSettingsCodepoint),
    0,
  };

  ImFontConfig oxygen_config {};
  oxygen_config.MergeMode = false;
  oxygen_config.PixelSnapH = true;
  oxygen_config.GlyphMinAdvanceX = kToolbarIconFontSize;
  constexpr std::string_view kOxygenIconsName { "oxygen-icons" };
  const auto name = std::span { oxygen_config.Name };
  auto [_, out] = std::ranges::copy_n(kOxygenIconsName.begin(),
    static_cast<std::ptrdiff_t>(
      std::min(kOxygenIconsName.size(), name.size() - 1)),
    name.begin());
  *out = '\0';

  [[maybe_unused]] ImFont* oxygen_icon_font
    = io.Fonts->AddFontFromMemoryCompressedTTF(
      static_cast<const void*>(icons::OxygenIcons_compressed_data),
      static_cast<int>(icons::OxygenIcons_compressed_size),
      kToolbarIconFontSize, &oxygen_config, oxygen_icon_ranges.data());
  IM_ASSERT(oxygen_icon_font != nullptr);
}

} // namespace oxygen::imgui
