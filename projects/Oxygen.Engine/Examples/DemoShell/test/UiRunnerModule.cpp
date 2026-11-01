//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/test/ContentPanel_ui.h"
#include "DemoShell/test/FileBrowser_ui.h"
#include "DemoShell/test/UiRunnerModule.h"
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/AsyncEngine.h>
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

UiRunnerModule::~UiRunnerModule()
{
  if (!fixture_root_.empty()) {
    std::error_code error;
    std::filesystem::remove_all(fixture_root_, error);
  }
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
  // The content panel's cooked sources and scratch project live in a folder
  // of the run; its tests write their sources there.
  fixture_root_ = std::filesystem::temp_directory_path()
    / ("oxygen-demoshell-ui-" + Uuid::Generate().ToString());
  std::filesystem::create_directories(fixture_root_ / "Content");
  shell_config.content_roots = {
    .content_root = fixture_root_,
    .cooked_root = fixture_root_ / ".cooked",
    .pak_directory = fixture_root_,
    .scratch_root = fixture_root_ / ".scratch",
  };
  shell_config.on_mounts_requested
    = [this](std::vector<content::ContentMount> mounts) -> void {
    pending_mounts_ = std::move(mounts);
  };
  shell_config.on_scene_load_requested
    = [this](const ui::SceneEntry& scene) -> void {
    pending_scene_loads_.push_back(scene.key);
  };
  shell_config.panel_config.content_loader = true;
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
  co_await ServeContentPanel();
  if (HasRenderableWindow() && !active_scene_.IsValid()
    && !Stage().HasStagedScene()) {
    StageScene();
  }
  co_await Base::OnSceneMutation(context);
}

auto UiRunnerModule::GetContentVm() -> observer_ptr<ui::ContentVm>
{
  return GetShell().GetContentVm();
}

auto UiRunnerModule::GetActivePanelName() -> std::optional<std::string>
{
  return GetShell().GetActivePanelName();
}

auto UiRunnerModule::ServeContentPanel() -> co::Co<>
{
  const auto vm = GetShell().GetContentVm();
  if (!vm) {
    co_return;
  }
  if (pending_mounts_) {
    auto mounts = std::move(*pending_mounts_);
    pending_mounts_.reset();
    auto outcome = runtime::MountOutcome {};
    if (const auto loader = Host().engine->GetAssetLoader()) {
      co_await loader->WaitForPendingLoadsAsync();
      outcome
        = co_await runtime::ApplyContentMounts(*loader, std::move(mounts));
    } else {
      outcome.error = "The engine has no asset loader";
    }
    vm->OnMountsApplied(outcome);
  }
  for (const auto& key : std::exchange(pending_scene_loads_, {})) {
    vm->NotifySceneLoadCompleted(key, true);
  }
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
  RegisterContentPanelUiTests(engine, *this);
}

} // namespace oxygen::examples::testing
