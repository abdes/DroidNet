//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::player::CanRemount;
using oxygen::player::LaunchFileError;
using oxygen::player::LaunchMount;
using oxygen::player::ParseLaunchFile;
using oxygen::player::ReadLaunchFile;

class LaunchFileTest : public testing::Test {
protected:
  void SetUp() override
  {
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen-player-launch-"
        + std::string(
          ::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_ / "cooked");
    std::filesystem::create_directories(root_ / "scripts");
    std::ofstream(root_ / "base.pak") << "pak";
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  [[nodiscard]] auto Parse(const std::string_view text) const
  {
    return ParseLaunchFile(text, root_, "launch.json");
  }

  //! The message of the error parsing `text` throws; empty when none.
  [[nodiscard]] auto ErrorOf(const std::string_view text) const -> std::string
  {
    try {
      static_cast<void>(Parse(text));
    } catch (const LaunchFileError& error) {
      return error.what();
    }
    return {};
  }

  std::filesystem::path root_;
};

NOLINT_TEST_F(LaunchFileTest, ReadsEveryFieldAndResolvesRelativePaths)
{
  const auto launch = Parse(R"({
    "version": 1,
    "mounts": ["base.pak", "cooked"],
    "scene": "/.cooked/Scenes/Level.oscene",
    "script_source_roots": ["scripts"],
    "window": { "title": "Level", "width": 1600, "height": 900,
                "vsync": false, "frame_cap": 60 },
    "diagnostics_pipe": "oxygen-player-1"
  })");

  ASSERT_EQ(launch.mounts.size(), 2U);
  EXPECT_EQ(launch.mounts[0].kind, LaunchMount::Kind::kPakFile);
  EXPECT_EQ(launch.mounts[0].path, (root_ / "base.pak").lexically_normal());
  EXPECT_EQ(launch.mounts[1].kind, LaunchMount::Kind::kLooseCookedRoot);
  EXPECT_EQ(launch.mounts[1].path, (root_ / "cooked").lexically_normal());
  EXPECT_EQ(launch.scene, "/.cooked/Scenes/Level.oscene");
  ASSERT_EQ(launch.script_source_roots.size(), 1U);
  EXPECT_EQ(
    launch.script_source_roots[0], (root_ / "scripts").lexically_normal());
  EXPECT_EQ(launch.window.title, "Level");
  EXPECT_EQ(launch.window.width, 1600U);
  EXPECT_EQ(launch.window.height, 900U);
  EXPECT_FALSE(launch.window.vsync);
  EXPECT_EQ(launch.window.frame_cap, 60U);
  EXPECT_EQ(launch.diagnostics_pipe, "oxygen-player-1");
  EXPECT_FALSE(CanRemount(launch));
}

NOLINT_TEST_F(LaunchFileTest, OptionalFieldsTakeTheirDefaults)
{
  const auto launch = Parse(R"({
    "version": 1, "mounts": ["cooked"], "scene": "/Scenes/A.oscene"
  })");

  EXPECT_TRUE(launch.script_source_roots.empty());
  EXPECT_EQ(launch.window.title, "Oxygen Player");
  EXPECT_EQ(launch.window.width, 1280U);
  EXPECT_EQ(launch.window.height, 720U);
  EXPECT_TRUE(launch.window.vsync);
  EXPECT_EQ(launch.window.frame_cap, 0U);
  EXPECT_FALSE(launch.diagnostics_pipe.has_value());
  EXPECT_TRUE(CanRemount(launch));
}

NOLINT_TEST_F(LaunchFileTest, ReadLaunchFileResolvesAgainstItsFolder)
{
  std::ofstream(root_ / "launch.json") << R"({
    "version": 1, "mounts": ["cooked"], "scene": "/Scenes/A.oscene"
  })";

  const auto launch = ReadLaunchFile(root_ / "launch.json");

  ASSERT_EQ(launch.mounts.size(), 1U);
  EXPECT_EQ(launch.mounts[0].path, (root_ / "cooked").lexically_normal());
}

NOLINT_TEST_F(LaunchFileTest, SchemaViolationsNameTheSchemaAndTheValue)
{
  const auto missing_scene
    = ErrorOf(R"({ "version": 1, "mounts": ["cooked"] })");
  EXPECT_NE(
    missing_scene.find("oxygen.player-launch.schema.json"), std::string::npos);
  EXPECT_NE(missing_scene.find("scene"), std::string::npos);

  const auto bad_width = ErrorOf(R"({ "version": 1, "mounts": ["cooked"],
    "scene": "/A.oscene", "window": { "width": "wide" } })");
  EXPECT_NE(bad_width.find("/window/width"), std::string::npos);

  const auto unknown = ErrorOf(R"({ "version": 1, "mounts": ["cooked"],
    "scene": "/A.oscene", "fullscreen": true })");
  EXPECT_NE(unknown.find("fullscreen"), std::string::npos);

  EXPECT_FALSE(ErrorOf(R"({ "version": 2, "mounts": ["cooked"],
    "scene": "/A.oscene" })")
      .empty());
  EXPECT_FALSE(ErrorOf(R"({ "version": 1, "mounts": [],
    "scene": "/A.oscene" })")
      .empty());
  EXPECT_FALSE(ErrorOf(R"({ "version": 1, "mounts": ["cooked"],
    "scene": "A.oscene" })")
      .empty());
}

NOLINT_TEST_F(LaunchFileTest, InvalidJsonIsReported)
{
  EXPECT_NE(ErrorOf("{ not json").find("not valid JSON"), std::string::npos);
}

NOLINT_TEST_F(LaunchFileTest, MissingContentIsReported)
{
  EXPECT_NE(ErrorOf(R"({ "version": 1, "mounts": ["absent"],
    "scene": "/A.oscene" })")
              .find("absent"),
    std::string::npos);
  EXPECT_NE(ErrorOf(R"({ "version": 1, "mounts": ["absent.pak"],
    "scene": "/A.oscene" })")
              .find("absent.pak"),
    std::string::npos);
  EXPECT_NE(ErrorOf(R"({ "version": 1, "mounts": ["cooked"],
    "scene": "/A.oscene", "script_source_roots": ["nowhere"] })")
              .find("nowhere"),
    std::string::npos);
}

NOLINT_TEST_F(LaunchFileTest, UnreadableFileIsReported)
{
  EXPECT_THROW(
    static_cast<void>(ReadLaunchFile(root_ / "missing.json")), LaunchFileError);
}

} // namespace
