//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/ImportTool/ImportRunner.cpp (RefuseImportJob)

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/CommandLineContext.h> // IWYU pragma: keep
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/Tools/ImportTool/ScriptCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/test/CapturingWriter.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace tool = oxygen::content::import::tool;
namespace report_codes = oxygen::content::toolkit::report_codes;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportSeverity;

class ImportRefusalTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] auto Writer() -> oxygen::observer_ptr<tool::IMessageWriter>
  {
    return oxygen::make_observer<tool::IMessageWriter>(&writer_);
  }

  [[nodiscard]] auto Messages() const -> std::string
  {
    return writer_.JoinedMessages();
  }

private:
  tool::test::CapturingWriter writer_;
};

//! A command that stops before its job states the refusal in its report.
NOLINT_TEST_F(ImportRefusalTest, ACommandRefusalIsAFailedEntryOfItsReport)
{
  const auto report_path = TempPath("report.json");
  auto options = tool::GlobalOptions {};
  options.no_tui = true;
  options.writer = Writer();
  options.command_line = "tool script";
  auto command = tool::ScriptCommand(&options);
  const auto cli = oxygen::clap::CliBuilder()
                     .ProgramName("tool")
                     .WithCommand(command.BuildCommand())
                     .Build();
  const auto report_arg = report_path.generic_string();
  auto argv = std::vector<const char*> {
    "tool",
    "script",
    "missing.luau",
    "--virtual-path",
    "/Content/Missing.oscript",
    "--mount",
    "no-separator",
    "--report",
    report_arg.c_str(),
  };
  static_cast<void>(cli->Parse(argv));

  const auto result = command.Execute();

  ASSERT_FALSE(result.has_value()) << Messages();
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open()) << Messages();
  const auto report = nlohmann::json::parse(stream);
  ASSERT_EQ(report.at("jobs").size(), 1U);
  const auto& job = report.at("jobs").at(0);
  EXPECT_EQ(job.at("type"), "script");
  EXPECT_EQ(job.at("status"), "failed");
  ASSERT_EQ(job.at("diagnostics").size(), 1U);
  EXPECT_EQ(job.at("diagnostics").at(0).at("code").get<std::string>(),
    tool::import_tool_codes::kOptionInvalid);
  EXPECT_EQ(report.at("summary").at("jobs_failed"), 1);
}

//! A report names files absolutely, whatever the command line's spelling.
NOLINT_TEST_F(ImportRefusalTest, ARefusalSpellsARelativeSourceAbsolute)
{
  const auto report_path = TempPath("report.json");
  const auto diagnostics = std::vector {
    ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "import.test_refusal",
      .message = "refused",
      .source_path = "relative/source.luau",
    },
  };
  const auto report_arg = report_path.string();

  static_cast<void>(tool::RefuseImportJob(
    {
      .report_path = report_arg,
      .command_line = "tool script",
      .job_type = "script",
      .source_path = "relative/source.luau",
      .cooked_root = "relative/cooked",
      .diagnostics = diagnostics,
    },
    Writer()));

  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open()) << Messages();
  const auto report = nlohmann::json::parse(stream);
  const auto source = std::filesystem::path {
    report.at("jobs")
      .at(0)
      .at("diagnostics")
      .at(0)
      .at("source_path")
      .get<std::string>(),
  };
  const auto cooked_root = std::filesystem::path {
    report.at("session").at("cooked_root").get<std::string>(),
  };
  EXPECT_TRUE(source.is_absolute());
  EXPECT_TRUE(cooked_root.is_absolute());
}

//! A job's report holds the absolute paths of a relative command line.
NOLINT_TEST_F(ImportRefusalTest, AJobSpellsARelativeCommandLineAbsolute)
{
  const auto report_path = TempPath("report.json");
  const auto report_arg = report_path.string();
  auto service = oxygen::content::import::AsyncImportService(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 1U,
      .max_in_flight_jobs = 1U,
    });
  auto request = oxygen::content::import::ImportRequest {};
  request.source_path = "relative/missing.png";
  request.cooked_root = std::filesystem::path("relative/cooked");
  const auto working_directory = std::filesystem::current_path();
  std::filesystem::current_path(TempDir());

  const auto result
    = tool::RunImportJob(request, Writer(), report_arg, "tool texture", false,
      oxygen::make_observer<oxygen::content::import::AsyncImportService>(
        &service));

  std::filesystem::current_path(working_directory);
  EXPECT_FALSE(result.has_value()) << Messages();
  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open()) << Messages();
  const auto report = nlohmann::json::parse(stream);
  const auto cooked_root = std::filesystem::path {
    report.at("session").at("cooked_root").get<std::string>(),
  };
  EXPECT_TRUE(cooked_root.is_absolute());
  const auto& issues = report.at("jobs").at(0).at("diagnostics");
  ASSERT_FALSE(issues.empty());
  for (const auto& issue : issues) {
    if (issue.contains("source_path")) {
      const auto source
        = std::filesystem::path(issue.at("source_path").get<std::string>());
      EXPECT_TRUE(source.is_absolute());
    }
  }
}

//! A refusal never destroys the file it refuses.
NOLINT_TEST_F(ImportRefusalTest, ARefusalKeepsAnInputItsReportWouldReplace)
{
  const auto source = TempPath("source.luau");
  {
    auto stream = std::ofstream(source);
    stream << "source";
  }
  const auto diagnostics = std::vector {
    ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "import.test_refusal",
      .message = "refused",
    },
  };
  const auto report_arg = source.string();

  const auto refused = tool::RefuseImportJob(
    {
      .report_path = report_arg,
      .command_line = "tool script",
      .job_type = "script",
      .source_path = source,
      .cooked_root = {},
      .diagnostics = diagnostics,
    },
    Writer());

  EXPECT_EQ(refused.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_NE(
    Messages().find(report_codes::kDestinationIsInput), std::string::npos)
    << Messages();
  auto stream = std::ifstream(source);
  const auto kept = std::string {
    std::istreambuf_iterator<char>(stream),
    std::istreambuf_iterator<char>(),
  };
  EXPECT_EQ(kept, "source");
}

} // namespace
