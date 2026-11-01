//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentMountList.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/test/ContentFixtures.h"
#include "DemoShell/test/ContentPanel_ui.h"
#include "DemoShell/test/UiRunnerModule.h"
#include <fmt/format.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

// NOLINTBEGIN(readability-magic-numbers) - wait bounds.

namespace oxygen::examples::testing {

namespace {
  constexpr unsigned kWaitFrames = 600U;

  //! Opens the content panel from the sidebar and makes it the reference.
  auto OpenContentPanel(ImGuiTestContext* ctx, UiRunnerModule& app) -> void
  {
    const auto active = app.GetActivePanelName();
    if (!active || *active != "Content Loader") {
      const auto icon = std::string(imgui::icons::kIconContentLoader);
      ctx->SetRef("//DemoPanelSideBar");
      ctx->ItemClick(icon.c_str());
    }
    ctx->Yield(3);
    ctx->SetRef("//Content Loader");
  }

  //! The child window of the content panel whose name contains `name`, such
  //! as the scene list or a scrolling table, whose items a path from the
  //! panel does not reach.
  auto FindPanelChild(ImGuiTestContext* ctx, const std::string_view name)
    -> ImGuiWindow*
  {
    auto* panel = ctx->GetWindowByRef("//Content Loader");
    if (panel == nullptr) {
      return nullptr;
    }
    // A child window is named `<parent>/<name>_<id>`; its own segment counts.
    const auto prefix = std::string(name) + "_";
    for (auto* window : ctx->UiContext->Windows) {
      const auto full = std::string_view(window->Name);
      const auto leaf = full.substr(full.rfind('/') + 1);
      // Each tab has its own body; only the open tab's was drawn.
      if (window != panel && window->RootWindow == panel->RootWindow
        && window->WasActive && leaf.starts_with(prefix)) {
        return window;
      }
    }
    return nullptr;
  }

  //! Clicks, or double-clicks, `path` in the panel's child window `child`,
  //! then makes the panel the reference again.
  auto ClickInChild(ImGuiTestContext* ctx, const std::string_view child,
    const std::string& path, const bool double_click = false) -> void
  {
    auto* window = FindPanelChild(ctx, child);
    IM_CHECK(window != nullptr);
    if (window == nullptr) {
      return;
    }
    ctx->SetRef(window);
    if (double_click) {
      ctx->ItemDoubleClick(path.c_str());
    } else {
      ctx->ItemClick(path.c_str());
    }
    ctx->SetRef("//Content Loader");
  }

  //! True when `path` names an item of the panel's child window `child`.
  auto ExistsInChild(ImGuiTestContext* ctx, const std::string_view child,
    const std::string& path) -> bool
  {
    auto* window = FindPanelChild(ctx, child);
    if (window == nullptr) {
      return false;
    }
    ctx->SetRef(window);
    const bool exists = ctx->ItemExists(path.c_str());
    ctx->SetRef("//Content Loader");
    return exists;
  }

  auto WaitForMounts(ImGuiTestContext* ctx, ui::ContentVm& vm) -> void
  {
    ctx->Yield(2);
    for (unsigned wait = 0U; wait < kWaitFrames && vm.IsMounting(); ++wait) {
      ctx->Yield();
    }
    ctx->Yield(2);
  }

  //! Leaves only `source` in the mount list, the engine pak beneath it.
  auto MountOnly(ImGuiTestContext* ctx, ui::ContentVm& vm,
    const ContentSourceSetting& source) -> void
  {
    while (!vm.Mounts().Added().empty()) {
      const auto row = vm.Mounts().IndexOf(vm.Mounts().Added().front().path);
      if (!row) {
        break;
      }
      vm.RemoveSource(*row);
      WaitForMounts(ctx, vm);
    }
    vm.AddSource(source);
    WaitForMounts(ctx, vm);
  }

  //! Mounts the refused `props.pak` alone and returns its row.
  auto MountRefusedPak(ImGuiTestContext* ctx, UiRunnerModule& app,
    ui::ContentVm& vm) -> std::optional<std::size_t>
  {
    const auto paks = WriteDependentPak(app.ContentFixtureRoot() / "refused");
    MountOnly(
      ctx, vm, { .kind = data::CookedSourceKind::kPak, .path = paks.pak });
    return vm.Mounts().IndexOf(paks.pak);
  }
} // namespace

auto RegisterContentPanelUiTests(ImGuiTestEngine* engine, UiRunnerModule& app)
  -> void
{
  // Scenario 1: find a scene by search and load it by double-click.
  auto* search = IM_REGISTER_TEST(engine, "content", "load_from_search");
  search->UserData = &app;
  search->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& runner = *static_cast<UiRunnerModule*>(ctx->Test->UserData);
    const auto vm = runner.GetContentVm();
    IM_CHECK(vm);
    const auto showcase
      = WriteShowcaseRoot(runner.ContentFixtureRoot() / "search" / "Showcase");
    MountOnly(ctx, *vm,
      {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = showcase.root,
      });
    const auto row = vm->Mounts().IndexOf(showcase.root);
    IM_CHECK(row.has_value());
    IM_CHECK(vm->Mounts().Rows().at(*row).state == ui::SourceState::kMounted);

    OpenContentPanel(ctx, runner);
    ctx->ItemClick("**/###Scenes");
    ctx->ItemInputValue("**/##SceneSearch", "bottle");
    ctx->Yield(3);
    // The search keeps the bottle's row and hides the lantern's.
    // A row's path in the scene list: its mount, its folder and its key.
    const auto lantern
      = "Showcase/Scenes/###" + data::to_string(showcase.lantern);
    const auto bottle
      = "Showcase/Scenes/###" + data::to_string(showcase.bottle);
    IM_CHECK(ExistsInChild(ctx, "SceneList", bottle));
    IM_CHECK(!ExistsInChild(ctx, "SceneList", lantern));
    ClickInChild(ctx, "SceneList", bottle, true);
    for (unsigned wait = 0U; wait < kWaitFrames && !vm->GetLoadedScene();
      ++wait) {
      ctx->Yield();
    }
    const auto& loaded = vm->GetLoadedScene();
    IM_CHECK(loaded.has_value());
    if (!loaded) {
      return;
    }
    IM_CHECK(loaded->scene.key == showcase.bottle);
    IM_CHECK(loaded->scene.virtual_path == showcase.bottle_path);
  };

  // Scenario 2: a pak whose requirement is missing shows why on Mounts.
  auto* refused = IM_REGISTER_TEST(engine, "content", "refused_pak_details");
  refused->UserData = &app;
  refused->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& runner = *static_cast<UiRunnerModule*>(ctx->Test->UserData);
    const auto vm = runner.GetContentVm();
    IM_CHECK(vm);
    const auto row = MountRefusedPak(ctx, runner, *vm);
    IM_CHECK(row.has_value());
    const auto& source = vm->Mounts().Rows().at(*row);
    IM_CHECK(source.state == ui::SourceState::kRefused);
    IM_CHECK_EQ(source.failures.size(), 1U);
    IM_CHECK(
      source.failures.front().failure == content::RequirementFailure::kMissing);

    OpenContentPanel(ctx, runner);
    ctx->ItemClick("**/###Mounts");
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists("**/Retry"));
    // The rows are in the mount list's table, each in its index's scope.
    ClickInChild(ctx, "MountList", fmt::format("MountTable/$${}/##row", *row));
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists("**/Retry"));
  };

  // Scenario 5: a log row's details lead to the source it concerns.
  auto* log = IM_REGISTER_TEST(engine, "content", "log_go_to_mount");
  log->UserData = &app;
  log->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& runner = *static_cast<UiRunnerModule*>(ctx->Test->UserData);
    const auto vm = runner.GetContentVm();
    IM_CHECK(vm);
    IM_CHECK(MountRefusedPak(ctx, runner, *vm).has_value());
    const ui::ContentLogEntry* refusal = nullptr;
    for (const auto& entry : vm->Log().Entries()) {
      if (entry.code == ui::log_codes::kMountRefused) {
        refusal = &entry;
      }
    }
    IM_CHECK(refusal != nullptr && refusal->cooked_source.has_value());

    OpenContentPanel(ctx, runner);
    ctx->ItemClick("**/###Log");
    ctx->Yield(2);
    // Each row is in its entry's ID scope.
    ClickInChild(ctx, "LogRows", fmt::format("$${}/##entry", refusal->id));
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists("**/Retry"));
    // Below the rows, out of view: a wildcard finds only items on screen.
    ClickInChild(ctx, "TabBody", "Go to the mount");
    ctx->Yield(3);
    // The Mounts tab is open on the refused pak's details.
    IM_CHECK(ctx->ItemExists("**/Retry"));
  };
}

} // namespace oxygen::examples::testing

// NOLINTEND(readability-magic-numbers)
