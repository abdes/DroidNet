//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <functional>
#include <system_error>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/ContentImportTab.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentLogTab.h"
#include "DemoShell/UI/ContentMountsTab.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelNavigation.h"
#include "DemoShell/UI/ContentScenesTab.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/test/ContentFixtures.h"
#include <imgui.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::testing {

namespace {

  namespace cc = ui::content_commands;

  //! An ImGui frame per draw, the demo settings reset, and a view model with
  //! no host: its sources are inspected but never mounted.
  class ContentLoaderPanelTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      ResetDemoSettings();
      root_ = std::filesystem::temp_directory_path()
        / ("oxygen-content-panel-" + Uuid::Generate().ToString());
      std::filesystem::create_directories(root_ / "Content");
      browser_.ConfigureContentRoots({
        .content_root = root_,
        .cooked_root = root_ / ".cooked",
        .pak_directory = root_,
        .scratch_root = root_ / ".scratch",
      });
      imgui_context_ = ImGui::CreateContext();
      ImGui::SetCurrentContext(imgui_context_);
      auto& io = ImGui::GetIO();
      io.DisplaySize = ImVec2 { 1280.0F, 720.0F };
      io.DeltaTime = 1.0F / 60.0F;
      io.BackendFlags = static_cast<ImGuiBackendFlags>(
        static_cast<unsigned int>(io.BackendFlags)
        | static_cast<unsigned int>(ImGuiBackendFlags_RendererHasTextures));
    }

    void TearDown() override
    {
      if (imgui_context_ != nullptr) {
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
      }
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      ResetDemoSettings();
    }

    static auto ResetDemoSettings() -> void
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      std::error_code ec;
      std::filesystem::remove(settings->GetStoragePath(), ec);
      settings->Load();
    }

    //! Draws `draw` inside a window of one ImGui frame.
    static auto DrawFrame(const std::function<void()>& draw) -> void
    {
      ImGui::NewFrame();
      ImGui::Begin("Content");
      draw();
      ImGui::End();
      ImGui::Render();
    }

    [[nodiscard]] auto Root() const -> const std::filesystem::path&
    {
      return root_;
    }

    ContentSettingsService settings_;
    FileBrowserService browser_;

  private:
    std::filesystem::path root_;
    ImGuiContext* imgui_context_ { nullptr };
  };

} // namespace

//! The panel and each of its tabs draw, with a source, its details and its
//! assets, and a log row.
NOLINT_TEST_F(ContentLoaderPanelTest, EachTabDraws)
{
  // Arrange
  const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
  ui::ContentVm vm { observer_ptr { &settings_ }, observer_ptr { &browser_ },
    nullptr };
  commands::CommandRegistry registry;
  PanelCommands commands { observer_ptr { &registry }, nullptr };
  ui::ContentLoaderPanel panel { observer_ptr { &vm },
    observer_ptr { &commands } };
  vm.AddSource({
    .kind = data::CookedSourceKind::kLooseCooked,
    .path = showcase.root,
  });
  vm.AddSource({
    .kind = data::CookedSourceKind::kPak,
    .path = Root() / "gone.pak",
  });
  auto navigation = ui::ContentPanelNavigation {
    .select_source = showcase.root,
  };
  ui::ContentScenesTab scenes { observer_ptr { &vm },
    observer_ptr { &commands } };
  ui::ContentMountsTab mounts { observer_ptr { &vm },
    observer_ptr { &commands }, observer_ptr { &navigation } };
  ui::ContentImportTab import { observer_ptr { &vm },
    observer_ptr { &commands } };
  ui::ContentLogTab log { observer_ptr { &vm }, observer_ptr { &commands },
    observer_ptr { &navigation } };

  // Act
  DrawFrame([&panel] -> void { panel.DrawContents(); });
  DrawFrame([&scenes] -> void { scenes.Draw(); });
  DrawFrame([&mounts] -> void { mounts.Draw(); });
  DrawFrame([&import] -> void { import.Draw(); });
  DrawFrame([&log] -> void { log.Draw(); });

  // Assert
  EXPECT_EQ(panel.GetName(), "Content Loader");
  EXPECT_FALSE(navigation.select_source.has_value());
  EXPECT_FALSE(vm.Log().Entries().empty());
  EXPECT_EQ(vm.Log().UnseenErrors(), 0U);
}

//! The panel's controls are commands any caller can run.
NOLINT_TEST_F(ContentLoaderPanelTest, CommandsChangeTheSettings)
{
  // Arrange
  ui::ContentVm vm { observer_ptr { &settings_ }, observer_ptr { &browser_ },
    nullptr };
  commands::CommandRegistry registry;
  PanelCommands commands { observer_ptr { &registry }, nullptr };
  const ui::ContentLoaderPanel panel { observer_ptr { &vm },
    observer_ptr { &commands } };

  // Act
  commands.RunBool(cc::kImportLights, false);
  commands.RunText(cc::kMaxSize, "1024");
  commands.RunBool(cc::kListFbx, false);
  commands.RunText(cc::kAddCookedRoot, (Root() / "Showcase").string());

  // Assert
  EXPECT_FALSE(settings_.GetImportProfile().options.import_lights);
  EXPECT_EQ(settings_.GetImportProfile().tuning.max_size, 1024U);
  EXPECT_FALSE(settings_.GetImportTab().fbx);
  ASSERT_EQ(settings_.GetMountSources().size(), 1U);
  EXPECT_EQ(settings_.GetMountSources().front().path, Root() / "Showcase");
}

//! Commands registered by the panel go away with its `PanelCommands`.
NOLINT_TEST_F(ContentLoaderPanelTest, CommandsAreUnregisteredWithTheirOwner)
{
  ui::ContentVm vm { observer_ptr { &settings_ }, observer_ptr { &browser_ },
    nullptr };
  commands::CommandRegistry registry;
  {
    PanelCommands commands { observer_ptr { &registry }, nullptr };
    const ui::ContentLoaderPanel panel { observer_ptr { &vm },
      observer_ptr { &commands } };
    EXPECT_NE(registry.Find(cc::kAddPak), nullptr);
    EXPECT_NE(registry.Find(cc::kLoadScene), nullptr);
    EXPECT_NE(registry.Find(cc::kClearLog), nullptr);
  }
  EXPECT_EQ(registry.Find(cc::kAddPak), nullptr);
  EXPECT_EQ(registry.Find(cc::kLoadScene), nullptr);
}

} // namespace oxygen::examples::testing
