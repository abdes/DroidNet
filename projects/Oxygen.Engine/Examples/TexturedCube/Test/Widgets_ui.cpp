//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <optional>
#include <string>

#include "DemoShell/Test/UiTestAssets.h"
#include "TexturedCube/MainModule.h"
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

namespace oxygen::examples::textured_cube {
// NOLINTBEGIN(readability-magic-numbers) - explicit test wait bounds.
auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  auto* test
    = IM_REGISTER_TEST(engine, "textured_cube", "assignment_panel_return");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    IM_CHECK(app.texture_vm_ != nullptr);
    const auto root = app.UiTestOutputDirectory() / "textures";
    testing::WriteUiTextureFixture(root);
    const auto text = root.string();
    auto& path = app.texture_vm_->GetImportState().cooked_root;
    IM_CHECK(text.size() < path.size());
    std::ranges::fill(path, '\0');
    std::ranges::copy(text, path.begin());
    // Fixture selection is setup. Actual assignment and refresh are UI input.
    app.GetShell().SetActivePanel("Texture Browser");
    ctx->Yield(3);
    ctx->SetRef("Texture Browser");
    ctx->ItemClick("**/Custom Texture");
    ctx->ItemClick("**/" ICON_FA_ARROW_ROTATE_RIGHT " Refresh List");
    ctx->Yield(3);
    IM_CHECK_EQ(app.texture_vm_->GetCookedEntries().size(), 2U);
    ctx->ItemClick("**/$$1/Sphere");
    ctx->ItemClick("**/$$2/Cube");
    for (int i = 0; i < 120
      && (app.texture_vm_->GetSphereTextureState().resource_key.get() == 0
        || app.texture_vm_->GetCubeTextureState().resource_key.get() == 0);
      ++i) {
      ctx->Yield();
    }
    const auto sphere = app.texture_vm_->GetSphereTextureState();
    const auto cube = app.texture_vm_->GetCubeTextureState();
    IM_CHECK(sphere.resource_key.get() != 0);
    IM_CHECK(cube.resource_key.get() != 0);
    IM_CHECK(sphere.resource_key != cube.resource_key);
    IM_CHECK_EQ(sphere.resource_index, 1U);
    IM_CHECK_EQ(cube.resource_index, 2U);
    const auto camera = std::string("//DemoPanelSideBar/")
      + std::string(imgui::icons::kIconCameraControls);
    ctx->ItemClick(camera.c_str());
    ctx->Yield(3);
    const auto browser = std::string("//DemoPanelSideBar/")
      + std::string(imgui::icons::kIconDemoPanel);
    ctx->ItemClick(browser.c_str());
    ctx->Yield(5);
    IM_CHECK(app.GetShell().GetActivePanelName()
      == std::optional<std::string>("Texture Browser"));
    IM_CHECK(app.texture_vm_->GetSphereTextureState().resource_key
      == sphere.resource_key);
    IM_CHECK(
      app.texture_vm_->GetCubeTextureState().resource_key == cube.resource_key);
    ctx->SetRef("Texture Browser");
    ctx->ItemClick("**/" ICON_FA_ARROW_ROTATE_RIGHT " Refresh List");
    ctx->Yield(5);
    IM_CHECK(app.texture_vm_->GetSphereTextureState().resource_key
      == sphere.resource_key);
    IM_CHECK(
      app.texture_vm_->GetCubeTextureState().resource_key == cube.resource_key);
  };
}
// NOLINTEND(readability-magic-numbers)
} // namespace oxygen::examples::textured_cube
