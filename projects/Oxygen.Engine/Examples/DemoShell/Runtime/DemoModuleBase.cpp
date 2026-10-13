//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/Services/SettingsService.h"
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/GpuWorkOverlay.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/Host/ViewportToolbar.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/SceneNode.h>

#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/test/UiTestSession.h"
#endif

namespace oxygen::examples {

namespace {

  using imgui::consoleui::LogSeverity;
  using imgui::consoleui::WindowPlacement;

  struct PlacementKeys {
    std::string_view x;
    std::string_view y;
    std::string_view width;
    std::string_view height;
  };

  constexpr PlacementKeys kConsoleWindowKeys {
    .x = "demo_shell.console.window.x",
    .y = "demo_shell.console.window.y",
    .width = "demo_shell.console.window.width",
    .height = "demo_shell.console.window.height",
  };
  constexpr PlacementKeys kPaletteWindowKeys {
    .x = "demo_shell.palette.window.x",
    .y = "demo_shell.palette.window.y",
    .width = "demo_shell.palette.window.width",
    .height = "demo_shell.palette.window.height",
  };
  constexpr std::string_view kAutoScrollKey = "demo_shell.console.auto_scroll";
  constexpr std::string_view kGpuWorkXKey = "demo_shell.gpu_work.x";
  constexpr std::string_view kGpuWorkYKey = "demo_shell.gpu_work.y";
  constexpr auto kSeverityFilters = std::array {
    std::pair { LogSeverity::kSuccess,
      std::string_view { "demo_shell.console.filter.ok" } },
    std::pair { LogSeverity::kWarning,
      std::string_view { "demo_shell.console.filter.warning" } },
    std::pair { LogSeverity::kError,
      std::string_view { "demo_shell.console.filter.error" } },
  };
  constexpr float kPlacementEpsilon = 0.5F;

  auto LoadPlacement(const SettingsService& settings, const PlacementKeys& keys)
    -> std::optional<WindowPlacement>
  {
    const auto x = settings.GetFloat(keys.x);
    const auto y = settings.GetFloat(keys.y);
    const auto width = settings.GetFloat(keys.width);
    const auto height = settings.GetFloat(keys.height);
    if (!x || !y || !width || !height) {
      return std::nullopt;
    }
    return WindowPlacement {
      .x = *x,
      .y = *y,
      .width = *width,
      .height = *height,
    };
  }

  auto Differs(const WindowPlacement& lhs, const WindowPlacement& rhs) -> bool
  {
    return std::abs(lhs.x - rhs.x) > kPlacementEpsilon
      || std::abs(lhs.y - rhs.y) > kPlacementEpsilon
      || std::abs(lhs.width - rhs.width) > kPlacementEpsilon
      || std::abs(lhs.height - rhs.height) > kPlacementEpsilon;
  }

  //! Saves `current` unless it matches `saved` or a drag is in progress.
  auto SavePlacement(SettingsService& settings, const PlacementKeys& keys,
    const std::optional<WindowPlacement>& current,
    std::optional<WindowPlacement>& saved) -> void
  {
    if (!current || (saved && !Differs(*current, *saved))
      || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      return;
    }
    settings.SetFloat(keys.x, current->x);
    settings.SetFloat(keys.y, current->y);
    settings.SetFloat(keys.width, current->width);
    settings.SetFloat(keys.height, current->height);
    saved = current;
  }

} // namespace

DemoModuleBase::DemoModuleBase(const host::HostContext& host)
  : HostModule(host)
{
}

DemoModuleBase::~DemoModuleBase() = default;

auto DemoModuleBase::OnHostAttached(observer_ptr<IAsyncEngine> engine) -> bool
{
#ifdef OXYGEN_BUILD_UI_TESTS
  if (testing::UiTestSession::Requested()) {
    SettingsService::ForDemoApp()->SetPersistenceEnabled(
      testing::UiTestSession::UsesIsolatedSettings());
  }
#endif
  if (!Host().owns_persistence) {
    // Another instance of this demo owns its settings file.
    SettingsService::ForDemoApp()->SetPersistenceEnabled(false);
  }
  shell_ = OnAttachedImpl(engine);
  if (!shell_) {
    LOG_F(ERROR, "{}: DemoShell initialization failed", GetName());
    return false;
  }
  LoadUiPlacement();
  return true;
}

auto DemoModuleBase::OnShutdown() noexcept -> void
{
#ifdef OXYGEN_BUILD_UI_TESTS
  StopUiTests();
#endif
  shell_.reset();
  HostModule::OnShutdown();
}

auto DemoModuleBase::MakeShellConfig() -> DemoShellConfig
{
  DemoShellConfig config;
  config.engine = observer_ptr<IAsyncEngine> { Host().engine.get() };
  config.stage = observer_ptr<const host::SceneStage> { &Stage() };
  config.commands = observer_ptr { Host().commands.get() };
  return config;
}

auto DemoModuleBase::GetShell() -> DemoShell&
{
  DCHECK_NOTNULL_F(shell_);
  return *shell_;
}

auto DemoModuleBase::OnFrameStart(
  const observer_ptr<engine::FrameContext> context) -> void
{
  HostModule::OnFrameStart(context);
  if (shell_) {
    shell_->OnFrameStart(*context);
  }
}

auto DemoModuleBase::OnScenePublished(engine::FrameContext& /*context*/) -> void
{
  if (shell_) {
    shell_->SyncActiveScene();
  }
}

auto DemoModuleBase::OnMainViewReady(const ViewId view_id,
  const scene::SceneNode& camera, const ViewPort& viewport) -> void
{
  if (shell_) {
    shell_->OnMainViewReady(view_id, camera, viewport);
  }
}

auto DemoModuleBase::OnInput(const observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (shell_) {
    shell_->Update(context->GetModuleTimingData().physical_delta_time);
  }
  co_return;
}

auto DemoModuleBase::OnGuiUpdate(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (!co_await DrawHostUi(context) || !shell_) {
    co_return;
  }
  shell_->Draw(context, UiTopInset());
  PersistUiPlacement();
}

auto DemoModuleBase::DrawHostUi(
  const observer_ptr<engine::FrameContext> context) -> co::Co<bool>
{
  co_await HostModule::OnGuiUpdate(context);
  const auto imgui_module = ResolveImGuiModule();
  if (!imgui_module || !imgui_module->IsFrameActive()) {
    co_return false;
  }
  // ImGui's current context is per binary: the host set its own, not ours.
  ImGui::SetCurrentContext(imgui_module->GetImGuiContext());
  co_return true;
}

auto DemoModuleBase::LoadUiPlacement() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }
  auto& state = ConsoleUi();
  saved_console_window_ = LoadPlacement(*settings, kConsoleWindowKeys);
  if (saved_console_window_) {
    state.SetConsoleWindowPlacement(*saved_console_window_);
  }
  saved_palette_window_ = LoadPlacement(*settings, kPaletteWindowKeys);
  if (saved_palette_window_) {
    state.SetPaletteWindowPlacement(*saved_palette_window_);
  }
  saved_auto_scroll_ = settings->GetBool(kAutoScrollKey).value_or(true);
  state.SetAutoScrollEnabled(saved_auto_scroll_);
  const auto gpu_work_x = settings->GetFloat(kGpuWorkXKey);
  const auto gpu_work_y = settings->GetFloat(kGpuWorkYKey);
  if (gpu_work_x && gpu_work_y) {
    const auto position = host::GpuWorkPosition { *gpu_work_x, *gpu_work_y };
    GpuWork().RestorePosition(position);
    saved_gpu_work_ = position;
  }
  for (std::size_t i = 0; i < kSeverityFilters.size(); ++i) {
    const auto& [severity, key] = kSeverityFilters.at(i);
    const bool enabled = settings->GetBool(key).value_or(true);
    state.SetSeverityEnabled(severity, enabled);
    saved_severity_filters_.at(i) = enabled;
  }
}

auto DemoModuleBase::PersistUiPlacement() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }
  const auto& state = ConsoleUi();
  SavePlacement(*settings, kConsoleWindowKeys, state.ConsoleWindowPlacement(),
    saved_console_window_);
  SavePlacement(*settings, kPaletteWindowKeys, state.PaletteWindowPlacement(),
    saved_palette_window_);
  if (const auto gpu_work = GpuWork().Position();
    gpu_work && gpu_work != saved_gpu_work_) {
    settings->SetFloat(kGpuWorkXKey, (*gpu_work)[0]);
    settings->SetFloat(kGpuWorkYKey, (*gpu_work)[1]);
    saved_gpu_work_ = gpu_work;
  }
  if (const bool auto_scroll = state.IsAutoScrollEnabled();
    auto_scroll != saved_auto_scroll_) {
    settings->SetBool(kAutoScrollKey, auto_scroll);
    saved_auto_scroll_ = auto_scroll;
  }
  for (std::size_t i = 0; i < kSeverityFilters.size(); ++i) {
    const auto& [severity, key] = kSeverityFilters.at(i);
    const bool enabled = state.IsSeverityEnabled(severity);
    if (enabled != saved_severity_filters_.at(i)) {
      settings->SetBool(key, enabled);
      saved_severity_filters_.at(i) = enabled;
    }
  }
}

#ifdef OXYGEN_BUILD_UI_TESTS
auto DemoModuleBase::StopUiTests() -> void { ui_tests_.reset(); }

auto DemoModuleBase::UiTestOutputDirectory() const -> std::filesystem::path
{
  return ui_tests_->OutputDirectory();
}

auto DemoModuleBase::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
try {
  if (!testing::UiTestSession::Requested() || Host().headless) {
    return;
  }
  if (!ui_tests_) {
    const auto imgui_module = ResolveImGuiModule();
    const auto window = Window() ? Window()->GetWindow() : nullptr;
    auto* context = imgui_module ? imgui_module->GetImGuiContext() : nullptr;
    if (context == nullptr || !window) {
      return;
    }
    ui_tests_ = std::make_unique<testing::UiTestSession>(
      *context, window->Native().window_handle);
    RegisterUiTests(ui_tests_->Engine());
    ui_tests_->Start();
  }
  if (ui_tests_->AfterPresent()) {
    Host().engine->Stop();
  }
} catch (const std::exception& error) {
  LOG_F(ERROR, "UI test session failed: {}", error.what());
  StopUiTests();
  Host().engine->Stop();
}
#endif

} // namespace oxygen::examples
