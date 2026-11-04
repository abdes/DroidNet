//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Common/ReportFile.cpp

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace fs = std::filesystem;
namespace report_codes = oxygen::content::toolkit::report_codes;
using oxygen::content::toolkit::CheckReportDestination;
using oxygen::content::toolkit::PublishReport;
using oxygen::content::toolkit::ReportGuard;
using oxygen::content::toolkit::ResolveReportPath;

auto WriteFile(const fs::path& path, const std::string_view text) -> void
{
  auto stream = std::ofstream(path, std::ios::binary);
  stream << text;
}

auto ReadFile(const fs::path& path) -> std::string
{
  auto stream = std::ifstream(path, std::ios::binary);
  return {
    std::istreambuf_iterator<char>(stream),
    std::istreambuf_iterator<char>(),
  };
}

class ReportFileTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] auto RefusalOf(
    const fs::path& destination, const ReportGuard& guard) const -> std::string
  {
    const auto checked = CheckReportDestination(destination, guard);
    return checked.has_value() ? std::string {}
                               : std::string { checked.error().code };
  }
};

NOLINT_TEST_F(ReportFileTest, RefusesAnInputUnderAnotherSpelling)
{
  const auto input = TempPath("sources/manifest.json");
  WriteFile(input, "{}");
  const auto guard = ReportGuard { .inputs = { input }, .cooked_roots = {} };

  EXPECT_EQ(RefusalOf(input, guard), report_codes::kDestinationIsInput);
  EXPECT_EQ(RefusalOf(TempDir() / "sources" / "." / "manifest.json", guard),
    report_codes::kDestinationIsInput);
  EXPECT_EQ(
    RefusalOf(TempDir() / "other" / ".." / "sources" / "manifest.json", guard),
    report_codes::kDestinationIsInput);
#if defined(_WIN32)
  EXPECT_EQ(RefusalOf(TempDir() / "SOURCES" / "MANIFEST.JSON", guard),
    report_codes::kDestinationIsInput);
#endif
}

//! A hard link names the same data as the input it links.
NOLINT_TEST_F(ReportFileTest, RefusesAHardLinkToAnInput)
{
  const auto input = TempPath("manifest.json");
  WriteFile(input, "{}");
  const auto link = TempPath("link.json");
  fs::create_hard_link(input, link);

  EXPECT_EQ(RefusalOf(link, { .inputs = { input }, .cooked_roots = {} }),
    report_codes::kDestinationIsInput);
}

NOLINT_TEST_F(ReportFileTest, AcceptsAMissingFileBesideItsInputs)
{
  const auto input = TempPath("manifest.json");
  WriteFile(input, "{}");

  EXPECT_TRUE(RefusalOf(TempPath("report.json"),
    {
      .inputs = { input, TempPath("missing.json"), fs::path {} },
      .cooked_roots = {},
    })
      .empty());
}

NOLINT_TEST_F(ReportFileTest, RefusesTheCookedRootAndAnythingInIt)
{
  const auto root = TempPath("cooked");
  fs::create_directories(root);
  const auto guard = ReportGuard { .inputs = {}, .cooked_roots = { root } };

  EXPECT_EQ(RefusalOf(root, guard), report_codes::kDestinationInCookedRoot);
  EXPECT_EQ(RefusalOf(root / "container.index.bin", guard),
    report_codes::kDestinationInCookedRoot);
  EXPECT_EQ(RefusalOf(root / "reports" / "report.json", guard),
    report_codes::kDestinationInCookedRoot);
}

//! `cooked-report.json` shares a text prefix with `cooked`, not a folder.
NOLINT_TEST_F(ReportFileTest, AcceptsASiblingThatSharesTheRootsPrefix)
{
  const auto root = TempPath("cooked");
  fs::create_directories(root);

  EXPECT_TRUE(RefusalOf(
    TempPath("cooked-report.json"), { .inputs = {}, .cooked_roots = { root } })
      .empty());
}

NOLINT_TEST_F(ReportFileTest, ResolvesARelativePathAgainstTheWorkingDirectory)
{
  EXPECT_EQ(ResolveReportPath("reports/../report.json"),
    (fs::current_path() / "report.json").lexically_normal());
}

NOLINT_TEST_F(ReportFileTest, PublishesIntoANewFolderAndReplacesAnOldReport)
{
  const auto report = TempDir() / "new" / "folder" / "report.json";

  ASSERT_TRUE(PublishReport(report, "first").has_value());
  EXPECT_EQ(ReadFile(report), "first");

  ASSERT_TRUE(PublishReport(report, "second").has_value());
  EXPECT_EQ(ReadFile(report), "second");
}

//! The publication replaces the name, so another name keeps the old bytes.
NOLINT_TEST_F(ReportFileTest, PublishingOverAHardLinkKeepsTheLinkedFile)
{
  const auto index = TempPath("cooked/container.index.bin");
  WriteFile(index, "index");
  const auto report = TempPath("report.json");
  fs::create_hard_link(index, report);

  ASSERT_TRUE(PublishReport(report, "report").has_value());

  EXPECT_EQ(ReadFile(report), "report");
  EXPECT_EQ(ReadFile(index), "index");
}

NOLINT_TEST_F(ReportFileTest, AWriteFailureIsTyped)
{
  const auto folder = TempPath("folder");
  fs::create_directories(folder);

  const auto published = PublishReport(folder, "report");

  ASSERT_FALSE(published.has_value());
  EXPECT_EQ(published.error().code, report_codes::kWriteFailed);
}

} // namespace
