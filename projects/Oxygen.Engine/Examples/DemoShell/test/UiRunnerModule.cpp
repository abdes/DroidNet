//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <utility>

#include "DemoShell/DemoShell.h"
#include "DemoShell/test/FileBrowser_ui.h"
#include "DemoShell/test/UiRunnerModule.h"
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::examples::testing {

namespace {
  constexpr std::size_t kSceneCapacity = 16;
  constexpr int kWarmupFrames = 90;
} // namespace

UiRunnerModule::UiRunnerModule(const host::HostContext& host)
  : Base(host)
{
}

auto UiRunnerModule::BuildWindowProperties() const
  -> platform::window::Properties
{
  auto props = Base::BuildWindowProperties();
  props.title = "DemoShell UI Tests";
  return props;
}

auto UiRunnerModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  auto shell = std::make_unique<DemoShell>();
  auto shell_config = MakeShellConfig();
  shell_config.panel_config.content_loader = false;
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.diagnostics = true;
  shell_config.panel_config.post_process = true;
  shell_config.panel_config.ground_grid = true;
  shell_config.enable_camera_rig = true;
  shell_config.force_environment_override = false;

  if (!shell->Initialize(shell_config)) {
    LOG_F(ERROR, "DemoShellUiRunner: DemoShell initialization failed");
    return nullptr;
  }
  StageScene();
  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  return shell;
}

auto UiRunnerModule::StageScene() -> void
{
  Stage().Stage(
    std::make_shared<scene::Scene>("DemoShellUiRunner", kSceneCapacity));
  auto camera_node = Stage().GetStagedScene()->CreateNode("MainCamera");
  const bool attached
    = camera_node.AttachCamera(std::make_unique<scene::PerspectiveCamera>());
  CHECK_F(attached, "Failed to attach a camera to MainCamera");
  Stage().SetStagedMainCamera(std::move(camera_node));
}

auto UiRunnerModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (HasRenderableWindow() && !active_scene_.IsValid()
    && !Stage().HasStagedScene()) {
    StageScene();
  }
  co_await Base::OnSceneMutation(context);
}

auto UiRunnerModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  main_camera_ = Stage().TakePublishedMainCamera();
}

auto UiRunnerModule::BuildViews(
  engine::FrameContext& /*context*/, host::HostViews& views) -> void
{
  if (!main_camera_.IsAlive()) {
    return;
  }
  // An unset viewport covers the window.
  auto main_view
    = vortex::CompositionView::ForScene(main_view_id_, View {}, main_camera_);
  views.views.push_back(std::move(main_view));
  views.main = main_view_id_;
}

auto UiRunnerModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  auto* smoke = IM_REGISTER_TEST(engine, "demoshell", "runtime_smoke");
  smoke->UserData = this;
  smoke->TestFunc = [](ImGuiTestContext* ctx) -> void {
    const auto& app = *static_cast<UiRunnerModule*>(ctx->Test->UserData);
    ctx->Yield(kWarmupFrames);
    IM_CHECK(app.ResolveRenderer());
    IM_CHECK(app.Window());
    IM_CHECK(app.HasRenderableWindow());
  };

  RegisterFileBrowserUiTests(engine);
}

} // namespace oxygen::examples::testing
