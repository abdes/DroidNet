//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <utility>

#include "DemoShell/FileBrowser/imfilebrowser.h"
#include "DemoShell/test/FileBrowser_ui.h"
#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Base/Uuid.h>

namespace oxygen::examples::testing {

namespace {
  namespace fs = std::filesystem;
  using ImGui::FileBrowser;
  using ImGui::FileBrowserFlags;

  //! A browser over a scratch directory tree, removed with the test.
  /*!
   The tree holds `alpha.txt`, `Beta.TXT`, `gamma.cpp`, `$hidden.txt`, `dirA`
   (with `inner.cpp`) and `dirB`.
  */
  struct BrowserVars {
    BrowserVars() = default;
    BrowserVars(const BrowserVars&) = delete;
    auto operator=(const BrowserVars&) -> BrowserVars& = delete;
    BrowserVars(BrowserVars&&) = delete;
    auto operator=(BrowserVars&&) -> BrowserVars& = delete;

    ~BrowserVars()
    {
      browser.reset();
      if (!root.empty()) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
      }
    }

    std::unique_ptr<FileBrowser> browser;
    fs::path root;
  };

  auto Touch(const fs::path& path) -> void { const std::ofstream file(path); }

  //! Creates the scratch tree and the browser, without opening it.
  auto Prepare(ImGuiTestContext* ctx, const FileBrowserFlags flags)
    -> BrowserVars&
  {
    auto& vars = ctx->GetVars<BrowserVars>();
    vars.root = fs::weakly_canonical(fs::temp_directory_path())
      / ("oxygen-filebrowser-ui-" + Uuid::Generate().ToString());
    fs::create_directories(vars.root / "dirA");
    fs::create_directories(vars.root / "dirB");
    Touch(vars.root / "alpha.txt");
    Touch(vars.root / "Beta.TXT");
    Touch(vars.root / "gamma.cpp");
    Touch(vars.root / "$hidden.txt");
    Touch(vars.root / "dirA" / "inner.cpp");
    vars.browser = std::make_unique<FileBrowser>(flags, vars.root);
    return vars;
  }

  //! Opens the browser and makes its window the reference for item lookups.
  auto OpenBrowser(ImGuiTestContext* ctx, BrowserVars& vars) -> void
  {
    vars.browser->Open();
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
  }

  auto DisplayBrowser(ImGuiTestContext* ctx) -> void
  {
    if (const auto& browser = ctx->GetVars<BrowserVars>().browser) {
      browser->Display();
    }
  }

  auto AddBrowserTest(ImGuiTestEngine* engine, const char* name,
    ImGuiTestTestFunc* test_func) -> void
  {
    auto* test = IM_REGISTER_TEST(engine, "filebrowser", name);
    test->SetVarsDataType<BrowserVars>();
    test->GuiFunc = DisplayBrowser;
    test->TestFunc = test_func;
  }

} // namespace

auto RegisterFileBrowserUiTests(ImGuiTestEngine* engine) -> void
{
  AddBrowserTest(
    engine, "double_click_confirms_a_file", [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      OpenBrowser(ctx, vars);
      IM_CHECK(vars.browser->IsOpened());
      IM_CHECK(!ctx->ItemExists("**/[F] $hidden.txt"));

      ctx->ItemDoubleClick("**/[F] alpha.txt");
      ctx->Yield(2);

      IM_CHECK(vars.browser->HasSelected());
      IM_CHECK(vars.browser->GetSelected() == vars.root / "alpha.txt");
      IM_CHECK(!vars.browser->IsOpened());
    });

  AddBrowserTest(engine, "navigates_into_and_out_of_directories",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      OpenBrowser(ctx, vars);

      ctx->ItemDoubleClick("**/[D] dirA");
      ctx->Yield(2);
      IM_CHECK(vars.browser->GetDirectory() == vars.root / "dirA");
      IM_CHECK(ctx->ItemExists("**/[F] inner.cpp"));
      IM_CHECK(!ctx->ItemExists("**/[F] alpha.txt"));

      ctx->ItemDoubleClick("**/[D] ..");
      ctx->Yield(2);
      IM_CHECK(vars.browser->GetDirectory() == vars.root);
      IM_CHECK(ctx->ItemExists("**/[F] alpha.txt"));
    });

  AddBrowserTest(
    engine, "type_filter_hides_other_files", [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      vars.browser->SetTypeFilters({ ".txt", ".cpp" });
      OpenBrowser(ctx, vars);
      // The generated first entry shows every listed type.
      IM_CHECK(ctx->ItemExists("**/[F] alpha.txt"));
      IM_CHECK(ctx->ItemExists("**/[F] gamma.cpp"));

      // BeginCombo has no label hook for wildcard lookup; use its exact ID.
      ctx->ItemClick("##type_filters");
      ctx->ItemClick("//$FOCUSED/.cpp");
      ctx->Yield(2);

      IM_CHECK(!ctx->ItemExists("**/[F] alpha.txt"));
      IM_CHECK(ctx->ItemExists("**/[F] gamma.cpp"));
    });

  AddBrowserTest(engine, "refresh_keeps_surviving_selection",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      OpenBrowser(ctx, vars);
      ctx->ItemClick("**/[F] alpha.txt");
      IM_CHECK(vars.browser->GetSelected() == vars.root / "alpha.txt");

      ctx->ItemClick("**/*");
      ctx->Yield(2);
      IM_CHECK(vars.browser->GetSelected() == vars.root / "alpha.txt");

      std::error_code ignored;
      fs::remove(vars.root / "alpha.txt", ignored);
      ctx->ItemClick("**/*");
      ctx->Yield(2);

      // The selected file is gone: nothing is selected anymore.
      IM_CHECK(vars.browser->GetSelected() == vars.root);
      IM_CHECK(!ctx->ItemExists("**/[F] alpha.txt"));
    });

  AddBrowserTest(engine, "ctrl_click_builds_a_multi_selection",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kMultipleSelection);
      OpenBrowser(ctx, vars);

      ctx->ItemClick("**/[F] alpha.txt");
      ctx->KeyDown(ImGuiMod_Ctrl);
      ctx->ItemClick("**/[F] gamma.cpp");
      ctx->KeyUp(ImGuiMod_Ctrl);
      ctx->Yield(2);

      const auto selected = vars.browser->GetMultiSelected();
      IM_CHECK_EQ(selected.size(), static_cast<std::size_t>(2));
      IM_CHECK(selected.front() == vars.root / "alpha.txt");
      IM_CHECK(selected.back() == vars.root / "gamma.cpp");

      ctx->KeyDown(ImGuiMod_Ctrl);
      ctx->ItemClick("**/[F] alpha.txt");
      ctx->KeyUp(ImGuiMod_Ctrl);
      ctx->Yield(2);
      IM_CHECK_EQ(
        vars.browser->GetMultiSelected().size(), static_cast<std::size_t>(1));
    });

  AddBrowserTest(
    engine, "shift_click_selects_a_range", [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kMultipleSelection);
      OpenBrowser(ctx, vars);

      // Files sort case-insensitively: alpha.txt, Beta.TXT, gamma.cpp.
      ctx->ItemClick("**/[F] alpha.txt");
      ctx->KeyDown(ImGuiMod_Shift);
      ctx->ItemClick("**/[F] gamma.cpp");
      ctx->KeyUp(ImGuiMod_Shift);
      ctx->Yield(2);

      IM_CHECK_EQ(
        vars.browser->GetMultiSelected().size(), static_cast<std::size_t>(3));
    });

  AddBrowserTest(
    engine, "typed_filename_is_confirmed", [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kEnterNewFilename);
      OpenBrowser(ctx, vars);

      ctx->ItemInputValue("**/##input_name", "created.txt");
      ctx->ItemClick("**/ok");
      ctx->Yield(2);

      IM_CHECK(vars.browser->HasSelected());
      IM_CHECK(vars.browser->GetSelected() == vars.root / "created.txt");
    });

  AddBrowserTest(engine, "new_directory_is_created_and_listed",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kCreateNewDir);
      OpenBrowser(ctx, vars);

      ctx->ItemClick("**/+");
      ctx->Yield(2);
      ctx->SetRef("//$FOCUSED");
      ctx->ItemInputValue("**/name", "created");
      ctx->ItemClick("**/ok");
      ctx->Yield(2);

      IM_CHECK(fs::is_directory(vars.root / "created"));
      ctx->SetRef("//$FOCUSED");
      IM_CHECK(ctx->ItemExists("**/[D] created"));
    });

  AddBrowserTest(engine, "cancel_closes_without_a_selection",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      OpenBrowser(ctx, vars);
      ctx->ItemClick("**/[F] alpha.txt");

      ctx->ItemClick("**/cancel");
      ctx->Yield(2);

      IM_CHECK(!vars.browser->HasSelected());
      IM_CHECK(!vars.browser->IsOpened());
    });

  AddBrowserTest(engine, "copied_browser_displays_independently",
    [](ImGuiTestContext* ctx) -> void {
      auto& vars = Prepare(ctx, FileBrowserFlags::kNone);
      auto copy = std::make_unique<FileBrowser>(*vars.browser);
      vars.browser = std::move(copy);
      OpenBrowser(ctx, vars);

      ctx->ItemDoubleClick("**/[D] dirB");
      ctx->Yield(2);

      IM_CHECK(vars.browser->IsOpened());
      IM_CHECK(vars.browser->GetDirectory() == vars.root / "dirB");
    });
}

} // namespace oxygen::examples::testing
