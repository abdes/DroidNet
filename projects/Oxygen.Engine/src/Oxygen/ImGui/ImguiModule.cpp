//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/ImGui/ImGuiModule.h>

namespace oxygen::imgui {

ImGuiModule::ImGuiModule(std::shared_ptr<Platform> platform,
  std::unique_ptr<ImGuiGraphicsBackend> graphics_backend)
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
    render_pass_ = std::make_unique<ImGuiPass>(graphics_backend_);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "ImGuiModule: failed to create ImGuiPass: {}", e.what());
    return false;
  }

  return true;
}

auto ImGuiModule::OnShutdown() noexcept -> void
{
  // Stop event processing immediately
  platform_backend_.reset();
  try {
    if (graphics_backend_) {
      graphics_backend_->Shutdown();
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "exception while shutting down ImGuiModule: {}", ex.what());
  }
}

auto ImGuiModule::OnFrameStart(engine::FrameContext& frame_context) -> void
{
  // We should always have a graphics backend
  DCHECK_NOTNULL_F(graphics_backend_);

  if (!platform_backend_) {
    // Maybe no window yet, or window is closing/closed. There is nothing to do
    // for ImGui with no window.
    LOG_F(WARNING, "platform backend not valid, skipping frame");
    return;
  }

  // Platform backend handles SDL events and sets up display info FIRST
  platform_backend_->NewFrame();

  // Graphics backend handles ImGui context and calls ImGui::NewFrame()
  graphics_backend_->NewFrame();
}

auto ImGuiModule::GetRenderPass() const noexcept -> observer_ptr<ImGuiPass>
{
  if (!platform_backend_ || !graphics_backend_) {
    return {};
  }
  return observer_ptr { render_pass_.get() };
}

auto ImGuiModule::SetWindowId(platform::WindowIdType window_id) -> void
{
  // Store the window ID for later use in OnFrameStart
  window_id_ = window_id;

  if (window_id == platform::kInvalidWindowId) {
    platform_backend_.reset();
    render_pass_->Disable();
    return;
  }

  // Create platform backend if needed, and if we have a valid window ID
  try {
    platform_backend_ = std::make_unique<sdl3::ImGuiSdl3Backend>(
      platform_, window_id_, graphics_backend_->GetImGuiContext());
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

} // namespace oxygen::imgui
