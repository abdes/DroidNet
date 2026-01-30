//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/RenderGraph.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/RenderingVm.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/MainModule.h"

namespace oxygen::examples::light_bench {

namespace {
  constexpr std::uint32_t kWindowWidth = 2560;
  constexpr std::uint32_t kWindowHeight = 1440;
} // namespace

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen LightBench");
  p.extent = { .width = kWindowWidth, .height = kWindowHeight };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  file_browser_service_ = std::make_unique<FileBrowserService>();

  shell_ = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.input_system = observer_ptr { app_.input_system.get() };
  shell_config.file_browser_service
    = observer_ptr { file_browser_service_.get() };
  shell_config.get_renderer
    = [this]() { return observer_ptr { ResolveRenderer() }; };
  shell_config.get_pass_config_refs = [this]() {
    ui::PassConfigRefs refs;
    if (auto render_graph = GetRenderGraph()) {
      refs.shader_pass_config
        = observer_ptr { render_graph->GetShaderPassConfig().get() };
      refs.light_culling_pass_config
        = observer_ptr { render_graph->GetLightCullingPassConfig().get() };
    }
    return refs;
  };
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = true,
    .environment = true,
    .lighting = true,
    .rendering = true,
  };
  shell_config.enable_camera_rig = true;

  if (!shell_->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return false;
  }

  light_bench_panel_
    = std::make_shared<LightBenchPanel>(observer_ptr { &light_scene_ });
  if (!shell_->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }

  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  if (shell_) {
    shell_->SetScene(std::unique_ptr<scene::Scene> {});
    shell_->GetCameraLifecycle().Clear();
    shell_->SetSkyboxService(observer_ptr<SkyboxService> { nullptr });
  }
  active_scene_ = {};
  light_scene_.ClearScene();
  registered_view_camera_ = scene::NodeHandle();
  light_bench_panel_.reset();
  shell_.reset();
  skybox_service_.reset();
  skybox_service_scene_ = nullptr;
  file_browser_service_.reset();
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (!active_scene_.IsValid()) {
    auto scene = light_scene_.CreateScene();
    if (shell_) {
      active_scene_ = shell_->SetScene(std::move(scene));
      light_scene_.SetScene(shell_->TryGetScene());
    }
  }

  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };

  if (skybox_service_scene_ != scene_ptr.get()) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr { asset_loader.get() }, scene_ptr);
      skybox_service_scene_ = scene_ptr.get();
    } else {
      skybox_service_.reset();
      skybox_service_scene_ = nullptr;
    }
    if (shell_) {
      shell_->SetSkyboxService(observer_ptr { skybox_service_.get() });
    }
  }

  context.SetScene(observer_ptr { scene_ptr.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_F(active_scene_.IsValid());

  UpdateFrameContext(context, [this](int w, int h) {
    if (shell_) {
      auto& camera_lifecycle = shell_->GetCameraLifecycle();
      camera_lifecycle.EnsureViewport(w, h);
      camera_lifecycle.ApplyPendingSync();
      camera_lifecycle.ApplyPendingReset();
    }
  });

  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (shell_) {
    shell_->Update(time::CanonicalDuration {});
  }

  light_scene_.Update();

  co_return;
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  if (shell_) {
    shell_->Update(context.GetGameDeltaTime());
  }
  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    co_return;
  }

  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;

  if (!imgui_module_ref) {
    co_return;
  }
  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    co_return;
  }

  if (auto* imgui_context = imgui_module.GetImGuiContext()) {
    ImGui::SetCurrentContext(imgui_context);
  }

  if (shell_) {
    shell_->Draw();
  }

  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& /*context*/) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;

  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();

    if (auto shader_pass_config = rg->GetShaderPassConfig();
      shader_pass_config) {
      shader_pass_config->clear_color
        = graphics::Color { 0.1F, 0.1F, 0.12F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  ApplyRenderModeFromPanel();
  EnsureViewCameraRegistered();

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::EnsureViewCameraRegistered() -> void
{
  if (!shell_) {
    return;
  }
  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive()) {
    return;
  }

  const auto camera_handle = active_camera.GetHandle();
  if (registered_view_camera_ != camera_handle) {
    registered_view_camera_ = camera_handle;
    UnregisterViewForRendering("camera changed");
  }

  RegisterViewForRendering(active_camera);
}

auto MainModule::ApplyRenderModeFromPanel() -> void
{
  if (!shell_) {
    return;
  }
  if (auto render_graph = GetRenderGraph()) {
    auto shader_pass_config = render_graph->GetShaderPassConfig();
    auto transparent_pass_config = render_graph->GetTransparentPassConfig();
    if (!shader_pass_config || !transparent_pass_config) {
      return;
    }

    using graphics::FillMode;
    const auto view_mode = shell_->GetRenderingViewMode();
    const FillMode mode = (view_mode == ui::RenderingViewMode::kWireframe)
      ? FillMode::kWireFrame
      : FillMode::kSolid;
    render_graph->SetWireframeEnabled(mode == FillMode::kWireFrame);
    shader_pass_config->fill_mode = mode;
    transparent_pass_config->fill_mode = mode;

    const bool force_clear = (mode == FillMode::kWireFrame);
    shader_pass_config->clear_color_target = true;
    shader_pass_config->auto_skip_clear_when_sky_pass_present = !force_clear;

    // Apply debug mode. Rendering debug modes take precedence if set.
    auto debug_mode = shell_->GetRenderingDebugMode();
    if (debug_mode == engine::ShaderDebugMode::kDisabled) {
      debug_mode = shell_->GetLightCullingVisualizationMode();
    }
    shader_pass_config->debug_mode = debug_mode;
  }
}

} // namespace oxygen::examples::light_bench
