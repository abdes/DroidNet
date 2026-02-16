//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Scripting/Loader/ExternalScriptLoader.h>
#include <Oxygen/Scripting/Module/LuauModule.h>

namespace oxygen::scripting::test {

using oxygen::scripting::ExternalScriptLoader;

class ExternalScriptLoaderTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch())
                             .count();
    temp_root_ = std::filesystem::temp_directory_path()
      / std::string("oxygen_scripting_loader_test_")
          .append(std::to_string(timestamp));
    std::filesystem::create_directories(temp_root_ / "scripts");
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(temp_root_, ec);
  }

  [[nodiscard]] auto MakeLoader() const -> ExternalScriptLoader
  {
    auto config = PathFinderConfig::Create()
                    .WithWorkspaceRoot(temp_root_)
                    .WithScriptsRootPath("scripts")
                    .BuildShared();
    return ExternalScriptLoader { std::move(config), temp_root_ };
  }

  static auto WriteFile(
    const std::filesystem::path& path, std::string_view contents) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::out | std::ios::binary);
    out << contents;
  }

  [[nodiscard]] auto TempRoot() const -> const std::filesystem::path&
  {
    return temp_root_;
  }

private:
  std::filesystem::path temp_root_;
};

NOLINT_TEST_F(
  ExternalScriptLoaderTest, LoadScriptResolvesConfiguredRootAndExtension)
{
  const auto script_path = TempRoot() / "scripts/game/bootstrap.luau";

  // clang-format off
  constexpr auto kScript = R"lua(
value = 10
value = value + 5
)lua";
  // clang-format on
  WriteFile(script_path, kScript);

  const auto loader = MakeLoader();
  const auto loaded = loader.LoadScript("game/bootstrap");

  ASSERT_TRUE(loaded.ok);
  EXPECT_EQ(loaded.source_text, kScript);
  EXPECT_EQ(loaded.chunk_name, script_path.generic_string());
  EXPECT_TRUE(loaded.error_message.empty());
}

NOLINT_TEST_F(ExternalScriptLoaderTest, LoadScriptMissingScriptFails)
{
  const auto loader = MakeLoader();
  const auto loaded = loader.LoadScript("missing/script");

  EXPECT_FALSE(loaded.ok);
  EXPECT_NE(
    loaded.error_message.find("unable to open script file"), std::string::npos);
}

NOLINT_TEST_F(ExternalScriptLoaderTest, LuauModuleExecuteScriptViaLoader)
{
  const auto script_path = TempRoot() / "scripts/runtime/hook.luau";

  // clang-format off
  constexpr auto kScript = R"lua(
counter = 1
counter = counter + 1
)lua";
  // clang-format on
  WriteFile(script_path, kScript);

  const auto loader = MakeLoader();

  LuauModule module { engine::ModulePriority { 100U } }; // NOLINT
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(loader, "runtime/hook");
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
  EXPECT_TRUE(result.message.empty());
}

NOLINT_TEST_F(ExternalScriptLoaderTest, LoadScriptEmptyIdFails)
{
  const auto loader = MakeLoader();
  const auto loaded = loader.LoadScript("");

  EXPECT_FALSE(loaded.ok);
  EXPECT_EQ(loaded.error_message, "script id is empty");
}

NOLINT_TEST_F(ExternalScriptLoaderTest, LoadScriptAbsolutePathFails)
{
  const auto loader = MakeLoader();
  const auto absolute_path = std::filesystem::absolute("some/path.luau");
  const auto loaded = loader.LoadScript(absolute_path.string());

  EXPECT_FALSE(loaded.ok);
  EXPECT_EQ(loaded.error_message,
    "script id must be a relative path under scripts root");
}

NOLINT_TEST_F(ExternalScriptLoaderTest, LoadScriptParentTraversalFails)
{
  const auto loader = MakeLoader();
  const auto loaded = loader.LoadScript("../traversal.luau");

  EXPECT_FALSE(loaded.ok);
  EXPECT_EQ(
    loaded.error_message, "script id must not contain parent path traversal");
}

} // namespace oxygen::scripting::test
