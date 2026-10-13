//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <memory>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Host/ViewportToolbar.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::host {

namespace {

  auto MakeWindow(const HostContext& host) -> std::shared_ptr<HostWindow>
  {
    if (host.headless) {
      return nullptr;
    }
    return std::make_shared<HostWindow>(observer_ptr { host.platform.get() },
      observer_ptr { host.engine.get() }, host.gfx_weak,
      host.window.resolution);
  }

} // namespace

HostModule::HostModule(const HostContext& host)
  : host_(host)
  , window_(MakeWindow(host))
  , publisher_(observer_ptr { host.engine.get() }, host.gfx_weak,
      observer_ptr { window_.get() })
{
  publisher_.SetMainViewReadyHandler(
    [this](const ViewId view_id, const scene::SceneNode& camera,
      const ViewPort& viewport) -> void {
      OnMainViewReady(view_id, camera, viewport);
    });
  publisher_.SetRetainInactiveViewPredicate(
    [this](
      const ViewId view_id) -> bool { return RetainInactiveView(view_id); });
}

HostModule::~HostModule() = default;

auto HostModule::BuildWindowProperties() const -> platform::window::Properties
{
  constexpr std::uint32_t kDefaultWidth = 1280U;
  constexpr std::uint32_t kDefaultHeight = 720U;
  platform::window::Properties props("Oxygen");
  props.extent = { .width = kDefaultWidth, .height = kDefaultHeight };
  props.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = host_.window.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  return props;
}

auto HostModule::OnAttached(const observer_ptr<IAsyncEngine> engine) noexcept
  -> bool
{
  try {
    if (window_ && !window_->Create(BuildWindowProperties())) {
      LOG_F(ERROR, "{}: could not create the host window", GetName());
      return false;
    }
    return OnHostAttached(engine);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: attach failed: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR, "{}: attach failed with an unknown exception", GetName());
  }
  return false;
}

auto HostModule::OnShutdown() noexcept -> void
{
  try {
    toolbar_.ReleaseGpuTimeline();
    console_host_.UnregisterCommands();
    publisher_.Shutdown();
    stage_.Clear();
    views_ = {};
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: shutdown failed: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR, "{}: shutdown failed with an unknown exception", GetName());
  }
}

auto HostModule::RegisterCommands(
  const observer_ptr<commands::CommandRegistry> registry) noexcept -> void
{
  try {
    console_host_.RegisterCommands(*registry);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: command registration failed: {}", GetName(), ex.what());
  }
}

auto HostModule::PostLine(
  std::string line, const commands::CommandSource source) const -> void
{
  if (host_.commands) {
    host_.commands->PostLine(
      std::move(line), commands::CommandContext { .source = source });
  }
}

auto HostModule::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  try {
    MainViewOptions::RegisterConsoleBindings(*console);
    ViewportToolbar::RegisterConsoleBindings(*console);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: console registration failed: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR, "{}: console registration failed with an unknown exception",
      GetName());
  }
}

auto HostModule::ApplyConsoleCVars(
  const observer_ptr<const console::Console> console) noexcept -> void
{
  try {
    main_view_options_ = MainViewOptions::FromConsole(*console);
  } catch (const std::exception& ex) {
    LOG_F(
      ERROR, "{}: reading console variables failed: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR,
      "{}: reading console variables failed with an unknown exception",
      GetName());
  }
}

auto HostModule::OnFrameStart(const observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  publisher_.BeginFrame(*context);
  if (stage_.PublishStaged()) {
    OnScenePublished(*context);
  }
  context->SetScene(stage_.TryGetScene());
}

auto HostModule::OnSceneMutation(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  views_.views.clear();
  views_.main.reset();
  if (window_ && !window_->GetWindow()) {
    co_return;
  }
  BuildViews(*context, views_);
  main_view_options_.ApplyToMain(views_.views, views_.main);
  co_return;
}

auto HostModule::OnGuiUpdate(const observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto imgui_module = ResolveImGuiModule();
  if (!imgui_module || !imgui_module->IsFrameActive()) {
    co_return;
  }
  auto* imgui_context = imgui_module->GetImGuiContext();
  if (imgui_context == nullptr) {
    co_return;
  }
  ImGui::SetCurrentContext(imgui_context);
  const auto run
    = [this](std::string line, const commands::CommandSource source) -> void {
    PostLine(std::move(line), source);
  };
  toolbar_.Draw(*host_.engine, *context, ResolveRenderer(), views_.main,
    console_host_.State(), run);
  console_host_.Draw(host_.engine->GetConsole(), imgui_context, run);
  co_return;
}

auto HostModule::OnPublishViews(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  publisher_.Publish(*context, views_.views, views_.main);
  co_return;
}

auto HostModule::OnCompositing(const observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  publisher_.Compose(
    [this, context](vortex::Renderer::RuntimeCompositionInput& input) -> void {
      AppendCompositionLayers(*context, input);
    });
  co_return;
}

auto HostModule::HasRenderableWindow() const noexcept -> bool
{
  return host_.headless
    || (window_ && window_->GetWindow() && !window_->IsShuttingDown());
}

auto HostModule::ResolveRenderer() const noexcept
  -> observer_ptr<vortex::Renderer>
{
  if (const auto renderer = host_.engine->GetModule<vortex::Renderer>()) {
    return observer_ptr { &renderer->get() };
  }
  return nullptr;
}

auto HostModule::ResolveImGuiModule() const noexcept
  -> observer_ptr<imgui::ImGuiModule>
{
  if (const auto module = host_.engine->GetModule<imgui::ImGuiModule>()) {
    return observer_ptr { &module->get() };
  }
  return nullptr;
}

} // namespace oxygen::host
