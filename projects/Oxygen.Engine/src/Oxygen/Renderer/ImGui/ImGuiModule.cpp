//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Icons/OxygenIcons.h>
#include <Oxygen/ImGui/Styles/FontAwesome-400.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>
#include <Oxygen/Platform/ImGui/ImGuiSdl3Backend.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Imgui/ImGuiPass.h>

namespace oxygen::engine::imgui {

ImGuiModule::ImGuiModule(std::shared_ptr<Platform> platform,
  std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend)
  : platform_(std::move(platform))
  , graphics_backend_(std::move(graphics_backend))
{
}

ImGuiModule::~ImGuiModule() { OnShutdown(); }

auto ImGuiModule::OnAttached(const observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  DCHECK_NOTNULL_F(graphics_backend_);

  CHECK_NOTNULL_F(engine);
  auto gfx_weak = engine->GetGraphics();
  CHECK_F(!gfx_weak.expired());

  // Initialize graphics backend with engine's Graphics instance
  try {
    graphics_backend_->Init(std::move(gfx_weak));
  } catch (const std::exception& e) {
    LOG_F(ERROR, "ImGuiModule: graphics backend Init failed: {}", e.what());
    return false;
  }

  // Create ImGuiPass with the graphics backend
  try {
    render_pass_
      = std::make_unique<renderer::imgui::ImGuiPass>(graphics_backend_);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "ImGuiModule: failed to create ImGuiPass: {}", e.what());
    return false;
  }

  namespace sp = oxygen::imgui::spectrum;
  namespace icons = oxygen::imgui::icons;
  namespace styles = oxygen::imgui::styles;

  if (auto* ctx = graphics_backend_->GetImGuiContext()) {
    ImGui::SetCurrentContext(ctx);

    constexpr float kDefaultFontSize = 16.0F;
    constexpr float kToolbarIconFontSize = 24.0F;
    ImGuiStyle& style = ImGui::GetStyle();
    sp::StyleColorsSpectrum(style);

    ImGuiIO& io = ImGui::GetIO();
    ImFont* default_font = sp::LoadFont(*io.Fonts, kDefaultFontSize);
    IM_ASSERT(default_font != nullptr);
    io.FontDefault = default_font;

    // NOLINTBEGIN(*-avoid-c-arrays,*-array-to-pointer-decay,*-magic-numbers)

    // 1. Merge FontAwesome Icons into the default font for easy use everywhere
    constexpr ImWchar fa_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig fa_config {};
    fa_config.MergeMode = true;
    fa_config.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryCompressedTTF(
      styles::FontAwesome_compressed_data,
      static_cast<int>(styles::FontAwesome_compressed_size), kDefaultFontSize,
      &fa_config, fa_ranges);

    // 2. Oxygen Icons (Separate font for toolbars/special UI)
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
    constexpr auto kOxygenIconsName = "oxygen-icons"sv;
    auto [_, out] = std::ranges::copy_n(kOxygenIconsName.data(),
      std::min(kOxygenIconsName.size(), std::size(oxygen_config.Name) - 1),
      oxygen_config.Name);
    *out = '\0';

    ImFont* oxygen_icon_font = io.Fonts->AddFontFromMemoryCompressedTTF(
      icons::OxygenIcons_compressed_data,
      static_cast<int>(icons::OxygenIcons_compressed_size),
      kToolbarIconFontSize, &oxygen_config, oxygen_icon_ranges);
    IM_ASSERT(oxygen_icon_font != nullptr);

    // NOLINTEND(*-avoid-c-arrays,*-array-to-pointer-decay,*-magic-numbers)
  }

  return true;
}

auto ImGuiModule::OnShutdown() noexcept -> void
{
  // Stop event processing immediately
  // Unregister possible platform pre-destroy handler first
  try {
    if (platform_window_destroy_handler_token_ != 0 && platform_) {
      platform_->UnregisterWindowAboutToBeDestroyedHandler(
        platform_window_destroy_handler_token_);
      platform_window_destroy_handler_token_ = 0;
    }
  } catch (const std::exception& e) {
    LOG_F(WARNING, "failed to unregister window destroy handler: {}", e.what());
  }

  // Platform backend: might be null if no window or already detached.
  LOG_F(2,
    "ImGuiModule::OnShutdown() - before platform_backend_.reset() (backend=%p)",
    static_cast<void*>(platform_backend_.get()));
  platform_backend_.reset();
  LOG_F(2, "ImGuiModule::OnShutdown() - after platform_backend_.reset()");
  try {
    if (graphics_backend_) {
      LOG_F(1,
        "ImGuiModule::OnShutdown() - calling graphics_backend_->Shutdown() "
        "(backend=%p)",
        static_cast<void*>(graphics_backend_.get()));
      graphics_backend_->Shutdown();
      LOG_F(INFO,
        "ImGuiModule::OnShutdown() - graphics_backend_->Shutdown() returned");
      loguru::flush();
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "exception while shutting down ImGuiModule: {}", ex.what());
  }
  LOG_F(INFO, "ImGuiModule::OnShutdown() - completed");
  loguru::flush();
}

auto ImGuiModule::OnFrameStart(
  observer_ptr<engine::FrameContext> /*frame_context*/) -> void
{
  // We should always have a graphics backend
  DCHECK_NOTNULL_F(graphics_backend_);

  if (!platform_backend_) {
    // Maybe no window yet, or window is closing/closed. There is nothing to do
    // for ImGui with no window.
    LOG_F(2, "platform backend not valid, skipping frame");
    render_pass_->Disable();
    return;
  }

  // Platform backend handles SDL events and sets up display info FIRST
  platform_backend_->NewFrame();

  // Before invoking the graphics backend's NewFrame (which will call
  // ImGui::NewFrame()) ensure ImGui has a valid display size / framebuffer
  // scale. During rapid resize/maximize sequences SDL or the platform code
  // can briefly report zero-sized framebuffers which triggers ImGui sanity
  // checks and can abort. Check the current ImGui context/IO and skip the
  // backend NewFrame if parameters are invalid.
  if (auto* ctx = GetImGuiContext()) {
    ImGui::SetCurrentContext(ctx);
    const auto& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F
      && io.DisplayFramebufferScale.x > 0.0F
      && io.DisplayFramebufferScale.y > 0.0F) {
      graphics_backend_->NewFrame();
      // Mark that we actually started an ImGui frame so we can EndFrame later
      frame_started_ = true;
    } else {
      // Skip: invalid display/framebuffer metrics (common during maximize)
      DLOG_F(2,
        "ImGuiModule::OnFrameStart - skipping NewFrame due to invalid display "
        "metrics: size=(%.1f,%.1f) scale=(%.2f,%.2f)",
        io.DisplaySize.x, io.DisplaySize.y, io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y);
    }
  } else {
    // No context -> nothing to do
    DLOG_F(2,
      "ImGuiModule::OnFrameStart - no ImGui context available, skipping "
      "graphics backend NewFrame");
  }

  // Note: We only set frame_started_ when the graphics backend's NewFrame
  // was called and therefore an ImGui::NewFrame began. Some render paths may
  // skip calling ImGui::Render (for example surface teardown / missing
  // framebuffers), so ensure we end the frame on FrameEnd if needed.
}

auto ImGuiModule::GetRenderPass() const noexcept
  -> observer_ptr<renderer::imgui::ImGuiPass>
{
  if (!platform_backend_ || !graphics_backend_) {
    return {};
  }
  return observer_ptr { render_pass_.get() };
}

auto ImGuiModule::SetWindowId(platform::WindowIdType window_id) -> void
{
  if (window_id_ == window_id) {
    return; // No change
  }

  // Store the window ID for later use in OnFrameStart
  window_id_ = window_id;

  if (window_id == platform::kInvalidWindowId) {
    // Unregister any pre-destroy handler we registered for the old window.
    try {
      if (platform_window_destroy_handler_token_ != 0) {
        platform_->UnregisterWindowAboutToBeDestroyedHandler(
          platform_window_destroy_handler_token_);
        platform_window_destroy_handler_token_ = 0;
      }
    } catch (const std::exception& e) {
      LOG_F(
        WARNING, "failed to unregister window destroy handler: {}", e.what());
    }

    platform_backend_.reset();
    render_pass_->Disable();
    return;
  }

  // Create platform backend if needed, and if we have a valid window ID
  try {
    platform_backend_ = std::make_unique<platform::imgui::ImGuiSdl3Backend>(
      platform_, window_id_, graphics_backend_->GetImGuiContext());
    // Register to be notified if the platform is about to destroy the
    // native window so we can clear our platform backend before the
    // destruction (avoids dangling pointers inside imgui_impl_sdl3).
    try {
      // If we had an old handler, unregister it first.
      if (platform_window_destroy_handler_token_ != 0) {
        platform_->UnregisterWindowAboutToBeDestroyedHandler(
          platform_window_destroy_handler_token_);
        platform_window_destroy_handler_token_ = 0;
      }
      platform_window_destroy_handler_token_
        = platform_->RegisterWindowAboutToBeDestroyedHandler(
          [this, window_id](platform::WindowIdType closing_window_id) {
            if (closing_window_id == window_id) {
              // The platform will destroy the native window later in the
              // same OnFrameEnd() sequence. Make sure we detach our
              // platform backend first so the SDL backend won't hold a
              // dangling pointer into the native window.
              SetWindowId(platform::kInvalidWindowId);
            }
          });
    } catch (const std::exception& ex) {
      LOG_F(
        WARNING, "failed to register window destroy handler: {}", ex.what());
      // Not fatal — ImGui will still try to work but risk dangling pointer
      // in extreme corner cases.
    }
    render_pass_->Enable();
  } catch (const std::exception& ex) {
    LOG_F(
      ERROR, "exception while creating ImGui platform backend: {}", ex.what());
  }
}

// FIXME: Temporarily, the ImGui context is unique and owned by the backend
auto ImGuiModule::GetImGuiContext() const noexcept -> ImGuiContext*
{
  return graphics_backend_ ? graphics_backend_->GetImGuiContext() : nullptr;
}

auto ImGuiModule::RecreateDeviceObjects() -> void
{
  if (!graphics_backend_) {
    return;
  }

  try {
    graphics_backend_->RecreateDeviceObjects();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "ImGuiModule: RecreateDeviceObjects failed: {}", ex.what());
  }
}

auto ImGuiModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
  // If we started an ImGui frame but no Render/EndFrame was performed by
  // the render code path, call EndFrame here to keep ImGui's internal
  // counters balanced and avoid assertion failures on the next NewFrame.
  if (!frame_started_) {
    return;
  }

  try {
    if (auto* ctx = GetImGuiContext()) {
      ImGui::SetCurrentContext(ctx);
      // EndFrame finalizes the ImGui frame; ImGui::Render may still be
      // called later by the backend to actually emit draw commands.
      ImGui::EndFrame();
    }
  } catch (const std::exception& ex) {
    LOG_F(1, "ImGuiModule::OnFrameEnd: EndFrame threw: {}", ex.what());
  } catch (...) {
    LOG_F(1, "ImGuiModule::OnFrameEnd: EndFrame threw unknown exception");
  }

  frame_started_ = false;
}

} // namespace oxygen::engine::imgui
