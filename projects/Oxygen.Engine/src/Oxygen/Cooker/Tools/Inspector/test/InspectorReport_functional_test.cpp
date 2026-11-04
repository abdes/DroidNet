//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/InspectorReport.cpp, CookedRootCommand.cpp

#include <array>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DependencyReport.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Cooker/Tools/Inspector/InventoryReport.h>
#include <Oxygen/Cooker/Tools/Inspector/SceneMetadata.h>
#include <Oxygen/Cooker/Tools/Inspector/ScriptMetadata.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace inspection = oxygen::content::inspection;
namespace toolkit = oxygen::content::toolkit;

constexpr auto kRefused = static_cast<int>(toolkit::ExitCode::kUsage);

class InspectorMissingRootTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] auto MissingRoot() const -> std::string
  {
    return TempPath("no-such-root").string();
  }

  [[nodiscard]] auto Output() const -> std::filesystem::path
  {
    return TempPath("report.json");
  }

  //! The exit code of `run`, as the tool reports it.
  template <typename Run>
  [[nodiscard]] static auto ExitCode(const Run& run) -> int
  {
    return toolkit::ExitCodeOf(std::cerr, run);
  }
};

//! A cooked root that does not exist is a refused input, as a missing request
//! file is, and leaves no report.
NOLINT_TEST_F(InspectorMissingRootTest, EveryReportRefusesIt)
{
  const auto root = MissingRoot();
  const auto output = Output().string();

  EXPECT_EQ(ExitCode([&] {
    return inspection::RunDependencyReport({
      .cooked_root = root,
      .output = output,
    });
  }),
    kRefused);
  EXPECT_EQ(ExitCode([&] {
    return inspection::RunInventoryReport({
      .cooked_root = root,
      .output = output,
    });
  }),
    kRefused);
  EXPECT_EQ(ExitCode([&] {
    return inspection::RunGeometryMetadataReport({
      .cooked_root = root,
      .output = output,
      .virtual_path = {},
    });
  }),
    kRefused);
  EXPECT_EQ(ExitCode([&] {
    return inspection::RunScriptMetadataReport({
      .cooked_root = root,
      .output = output,
      .virtual_path = {},
    });
  }),
    kRefused);
  EXPECT_EQ(ExitCode([&] {
    return inspection::RunSceneMetadataReport({
      .cooked_root = root,
      .output = output,
      .virtual_path = {},
    });
  }),
    kRefused);

  EXPECT_FALSE(std::filesystem::exists(Output()));
}

//! A dump command refuses it too, before it reads anything.
NOLINT_TEST_F(InspectorMissingRootTest, ADumpCommandRefusesIt)
{
  auto command = inspection::CookedRootCommand({
    .name = "dump",
    .about = "A dump under test.",
    .dump = [](std::ostream& out, const std::string&) -> toolkit::ToolResult {
      out << "dumped";
      return {};
    },
  });
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};
  const auto root = MissingRoot();
  const auto args = std::array<const char*, 3> {
    "Test.Tool",
    "dump",
    root.c_str(),
  };
  const auto commands = std::array<toolkit::ToolCommand*, 1> { &command };

  const auto code = toolkit::RunTool(
    toolkit::ToolInfo {
      .program = "Test.Tool",
      .version = "1.0",
      .about = "A tool under test.",
    },
    commands, args,
    toolkit::ToolStreams {
      .out = oxygen::observer_ptr<std::ostream> { &out },
      .err = oxygen::observer_ptr<std::ostream> { &err },
    });

  EXPECT_EQ(code, kRefused);
  EXPECT_THAT(out.str(), ::testing::Not(::testing::HasSubstr("dumped")));
  EXPECT_THAT(err.str(), ::testing::HasSubstr("no-such-root"));
}

} // namespace
