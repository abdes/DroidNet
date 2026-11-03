//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::player::LaunchFileError;
using oxygen::player::LaunchMount;
using oxygen::player::LaunchWindowMode;
using oxygen::player::ParseLaunchFile;
using oxygen::player::ReadLaunchFile;

class LaunchFileTest : public oxygen::testing::TempDirTest {
protected:
  void SetUp() override
  {
    std::filesystem::create_directories(TempDir() / "cooked");
    std::filesystem::create_directories(TempDir() / "scripts");
    std::ofstream(TempDir() / "base.pak") << "pak";
  }

  [[nodiscard]] auto Parse(const std::string_view text) const
  {
    return ParseLaunchFile(text, TempDir(), "launch.json");
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
};

NOLINT_TEST_F(LaunchFileTest, ReadsEveryFieldAndResolvesRelativePaths)
{
  const auto launch = Parse(R"({
    "version": 3,
    "mounts": ["base.pak", "cooked"],
    "scene": "/Cooked/Scenes/Level.oscene",
    "source_mounts": { "Content": "scripts" },
    "window": { "title": "Level", "mode": "borderless", "width": 1600,
                "height": 900, "vsync": false, "frame_cap": 60 },
    "control": { "port": 4711, "exit_when_owner_leaves": true }
  })");

  ASSERT_EQ(launch.mounts.size(), 2U);
  EXPECT_EQ(launch.mounts[0].kind, LaunchMount::Kind::kPakFile);
  EXPECT_EQ(launch.mounts[0].path, (TempDir() / "base.pak").lexically_normal());
  EXPECT_EQ(launch.mounts[1].kind, LaunchMount::Kind::kLooseCookedRoot);
  EXPECT_EQ(launch.mounts[1].path, (TempDir() / "cooked").lexically_normal());
  EXPECT_EQ(launch.scene, "/Cooked/Scenes/Level.oscene");
  ASSERT_EQ(launch.source_mounts.size(), 1U);
  ASSERT_TRUE(launch.source_mounts.contains("Content"));
  EXPECT_EQ(launch.source_mounts.at("Content"),
    (TempDir() / "scripts").lexically_normal());
  EXPECT_EQ(launch.window.title, "Level");
  EXPECT_EQ(launch.window.mode, LaunchWindowMode::kBorderless);
  EXPECT_EQ(launch.window.width, 1600U);
  EXPECT_EQ(launch.window.height, 900U);
  EXPECT_FALSE(launch.window.vsync);
  EXPECT_EQ(launch.window.frame_cap, 60U);
  if (!launch.control.has_value()) {
    FAIL() << "The launch file has no control block";
  }
  const auto& control = launch.control.value();
  EXPECT_EQ(control.port, 4711U);
  EXPECT_TRUE(control.exit_when_owner_leaves);
}

NOLINT_TEST_F(LaunchFileTest, OptionalFieldsTakeTheirDefaults)
{
  const auto launch = Parse(R"({
    "version": 3, "mounts": ["cooked"], "scene": "/Scenes/A.oscene"
  })");

  EXPECT_TRUE(launch.source_mounts.empty());
  EXPECT_EQ(launch.window.title, "Oxygen Player");
  EXPECT_EQ(launch.window.mode, LaunchWindowMode::kWindowed);
  EXPECT_EQ(launch.window.width, 1280U);
  EXPECT_EQ(launch.window.height, 720U);
  EXPECT_TRUE(launch.window.vsync);
  EXPECT_EQ(launch.window.frame_cap, 0U);
  EXPECT_FALSE(launch.control.has_value());
}

NOLINT_TEST_F(LaunchFileTest, ReadLaunchFileResolvesAgainstItsFolder)
{
  std::ofstream(TempDir() / "launch.json") << R"({
    "version": 3, "mounts": ["cooked"], "scene": "/Scenes/A.oscene"
  })";

  const auto launch = ReadLaunchFile(TempDir() / "launch.json");

  ASSERT_EQ(launch.mounts.size(), 1U);
  EXPECT_EQ(launch.mounts[0].path, (TempDir() / "cooked").lexically_normal());
}

NOLINT_TEST_F(LaunchFileTest, SchemaViolationsNameTheSchemaAndTheValue)
{
  const auto missing_scene
    = ErrorOf(R"({ "version": 3, "mounts": ["cooked"] })");
  EXPECT_NE(
    missing_scene.find("oxygen.player-launch.schema.json"), std::string::npos);
  EXPECT_NE(missing_scene.find("scene"), std::string::npos);

  const auto bad_width = ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "/A.oscene", "window": { "width": "wide" } })");
  EXPECT_NE(bad_width.find("/window/width"), std::string::npos);

  const auto unknown = ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "/A.oscene", "fullscreen": true })");
  EXPECT_NE(unknown.find("fullscreen"), std::string::npos);

  EXPECT_FALSE(ErrorOf(R"({ "version": 2, "mounts": ["cooked"],
    "scene": "/A.oscene" })")
      .empty());
  // Source mounts are an object of mount name to folder, never a root list.
  const auto root_list = ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "/A.oscene", "source_mounts": ["scripts"] })");
  EXPECT_NE(root_list.find("/source_mounts"), std::string::npos);
  const auto old_key = ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "/A.oscene", "script_source_roots": ["scripts"] })");
  EXPECT_NE(old_key.find("script_source_roots"), std::string::npos);
  EXPECT_FALSE(ErrorOf(R"({ "version": 3, "mounts": [],
    "scene": "/A.oscene" })")
      .empty());
  EXPECT_FALSE(ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "A.oscene" })")
      .empty());
}

NOLINT_TEST_F(LaunchFileTest, InvalidJsonIsReported)
{
  EXPECT_NE(ErrorOf("{ not json").find("not valid JSON"), std::string::npos);
}

NOLINT_TEST_F(LaunchFileTest, MissingContentIsReported)
{
  EXPECT_NE(ErrorOf(R"({ "version": 3, "mounts": ["absent"],
    "scene": "/A.oscene" })")
              .find("absent"),
    std::string::npos);
  EXPECT_NE(ErrorOf(R"({ "version": 3, "mounts": ["absent.pak"],
    "scene": "/A.oscene" })")
              .find("absent.pak"),
    std::string::npos);
  const auto missing_mount = ErrorOf(R"({ "version": 3, "mounts": ["cooked"],
    "scene": "/A.oscene", "source_mounts": { "Content": "nowhere" } })");
  EXPECT_NE(missing_mount.find("source mount 'Content'"), std::string::npos);
  EXPECT_NE(missing_mount.find("nowhere"), std::string::npos);
}

NOLINT_TEST_F(LaunchFileTest, UnreadableFileIsReported)
{
  EXPECT_THROW(static_cast<void>(ReadLaunchFile(TempDir() / "missing.json")),
    LaunchFileError);
}

//! A launch file past the limit is refused before it is read.
NOLINT_TEST_F(LaunchFileTest, FilePastTheLimitIsReported)
{
  const auto path = TempDir() / "large.json";
  std::ofstream(path, std::ios::binary)
    << std::string(oxygen::player::kMaxLaunchFileBytes.get() + 1U, ' ');

  try {
    static_cast<void>(ReadLaunchFile(path));
    ADD_FAILURE() << "an oversized launch file was read";
  } catch (const LaunchFileError& error) {
    EXPECT_NE(std::string_view(error.what()).find("large.json"),
      std::string_view::npos);
  }
}

} // namespace
