//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace oxygen::examples::testing {

namespace {

auto SourceRoot() -> std::filesystem::path
{
  return std::filesystem::path { __FILE__ }.parent_path().parent_path()
    .parent_path().parent_path();
}

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

} // namespace

NOLINT_TEST(AsyncVortexMigrationSurface, RuntimeLaneRejectsLegacyRoutingMarkers)
{
  const auto root = SourceRoot();
  const auto async_main = ReadTextFile(root / "Examples/Async/MainModule.cpp");
  const auto async_bootstrap = ReadTextFile(root / "Examples/Async/main_impl.cpp");
  const auto demo_shell_header = ReadTextFile(root / "Examples/DemoShell/DemoShell.h");
  const auto demo_shell_source = ReadTextFile(root / "Examples/DemoShell/DemoShell.cpp");
  const auto app_window_header = ReadTextFile(root / "Examples/DemoShell/Runtime/AppWindow.h");
  const auto app_window_source = ReadTextFile(root / "Examples/DemoShell/Runtime/AppWindow.cpp");

  constexpr std::array<std::string_view, 4> forbidden_markers {
    "CompositionView",
    "get_active_pipeline",
    "oxygen::renderer",
    "Oxygen/Renderer",
  };

  for (const auto marker : forbidden_markers) {
    EXPECT_FALSE(async_main.contains(marker)) << marker;
    EXPECT_FALSE(async_bootstrap.contains(marker)) << marker;
    EXPECT_FALSE(demo_shell_header.contains(marker)) << marker;
    EXPECT_FALSE(demo_shell_source.contains(marker)) << marker;
    EXPECT_FALSE(app_window_header.contains(marker)) << marker;
    EXPECT_FALSE(app_window_source.contains(marker)) << marker;
  }
}

NOLINT_TEST(AsyncVortexMigrationSurface,
  RuntimeLanePublishesViewsThroughDirectVortexSeams)
{
  const auto root = SourceRoot();
  const auto async_main = ReadTextFile(root / "Examples/Async/MainModule.cpp");

  EXPECT_TRUE(async_main.contains("UpsertPublishedRuntimeView("));
  EXPECT_TRUE(async_main.contains("ResolvePublishedRuntimeViewId("));
  EXPECT_TRUE(async_main.contains("RegisterResolvedView("));
  EXPECT_TRUE(async_main.contains("RegisterRuntimeComposition("));
  EXPECT_FALSE(async_main.contains("PublishRuntimeCompositionView("));
}

NOLINT_TEST(AsyncVortexMigrationSurface,
  RuntimeLaneLetsVortexOwnImGuiPlumbingAndComposition)
{
  const auto root = SourceRoot();
  const auto async_main = ReadTextFile(root / "Examples/Async/MainModule.cpp");
  const auto async_bootstrap = ReadTextFile(root / "Examples/Async/main_impl.cpp");
  const auto app_window_source
    = ReadTextFile(root / "Examples/DemoShell/Runtime/AppWindow.cpp");
  const auto renderer_source = ReadTextFile(root / "src/Oxygen/Vortex/Renderer.cpp");

  EXPECT_TRUE(async_main.contains("shell.OnRuntimeMainViewReady("));
  EXPECT_FALSE(async_main.contains("ImGuiOverlayPass"));
  EXPECT_FALSE(async_main.contains("MakeTextureBlend("));
  EXPECT_FALSE(async_main.contains("RegisterComposition("));
  EXPECT_TRUE(async_bootstrap.contains(".enable_imgui = !app.headless"));
  EXPECT_FALSE(async_bootstrap.contains("CreateImGuiRuntimeModule("));
  EXPECT_TRUE(app_window_source.contains("renderer.SetImGuiWindowId(GetWindowId())"));
  EXPECT_TRUE(renderer_source.contains("CreateImGuiGraphicsBackend()"));
  EXPECT_TRUE(renderer_source.contains("imgui_runtime_->RenderOverlay("));
}

NOLINT_TEST(AsyncVortexMigrationSurface,
  DemoShellUiKeepsOverlayButDisablesRendererBoundPanelsOnRuntimePath)
{
  const auto root = SourceRoot();
  const auto async_main = ReadTextFile(root / "Examples/Async/MainModule.cpp");
  const auto async_bootstrap = ReadTextFile(root / "Examples/Async/main_impl.cpp");
  const auto async_settings
    = ReadTextFile(root / "Examples/Async/AsyncDemoSettingsService.cpp");
  const auto demo_shell_header = ReadTextFile(root / "Examples/DemoShell/DemoShell.h");
  const auto demo_shell_source = ReadTextFile(root / "Examples/DemoShell/DemoShell.cpp");
  const auto demo_shell_ui_header
    = ReadTextFile(root / "Examples/DemoShell/UI/DemoShellUi.h");
  const auto demo_shell_ui_source
    = ReadTextFile(root / "Examples/DemoShell/UI/DemoShellUi.cpp");

  EXPECT_TRUE(async_bootstrap.contains(".enable_imgui = !app.headless"));
  EXPECT_TRUE(demo_shell_source.contains("DemoShellUi"));
  EXPECT_TRUE(demo_shell_ui_source.contains("GetModule<vortex::Renderer>()"));
  EXPECT_TRUE(demo_shell_ui_source.contains("renderer.GetImGuiContext()"));
  EXPECT_TRUE(demo_shell_ui_source.contains("stats_overlay.Draw(fc);"));
  EXPECT_TRUE(async_settings.contains(
    "return settings->GetBool(kSpotlightEnabledKey).value_or(true);"));
  EXPECT_TRUE(async_settings.contains(
    "return settings->GetBool(kSpotlightShadowsKey).value_or(false);"));

  EXPECT_TRUE(demo_shell_header.contains("enable_renderer_bound_panels"));
  EXPECT_TRUE(async_main.contains("enable_renderer_bound_panels = false;"));
  EXPECT_TRUE(demo_shell_ui_header.contains("MakeRuntimePanelConfig("));
  EXPECT_TRUE(demo_shell_ui_source.contains("panel_config.rendering = false;"));
  EXPECT_TRUE(demo_shell_ui_source.contains("panel_config.lighting = false;"));
  EXPECT_TRUE(demo_shell_ui_source.contains("panel_config.ground_grid = false;"));
}

} // namespace oxygen::examples::testing
