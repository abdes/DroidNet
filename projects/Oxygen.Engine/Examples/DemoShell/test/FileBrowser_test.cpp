//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "DemoShell/FileBrowser/imfilebrowser.h"

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using ImGui::FileBrowser;
using ImGui::FileBrowserFlags;
namespace detail = ImGui::file_browser_detail;
namespace fs = std::filesystem;

//=== Pure helpers ===-------------------------------------------------------//

NOLINT_TEST(FileBrowserHelpersTest, ToLowerAsciiOnlyTouchesAsciiLetters)
{
  EXPECT_EQ(detail::ToLowerAscii("AbC.TXT_1"), "abc.txt_1");
  // Bytes of multi-byte UTF-8 sequences must survive untouched.
  EXPECT_EQ(detail::ToLowerAscii("\xC3\x89.TXT"), "\xC3\x89.txt");
}

NOLINT_TEST(FileBrowserHelpersTest, Utf8PathConversionRoundTrips)
{
  const std::string utf8 = "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC.txt";

  const fs::path path = detail::PathFromUtf8(utf8);

  EXPECT_EQ(detail::PathToUtf8(path), utf8);
  EXPECT_EQ(detail::PathFromUtf8(""), fs::path {});
}

NOLINT_TEST(FileBrowserHelpersTest, SortKeyPutsDirectoriesFirst)
{
  const auto directory = detail::MakeSortKey(true, "zzz");
  const auto file = detail::MakeSortKey(false, "aaa");

  EXPECT_LT(directory, file);
}

NOLINT_TEST(FileBrowserHelpersTest, SortKeyIsCaseInsensitiveLowercaseFirst)
{
  std::vector<std::string> names { "b0", "a0", "A1" };

  std::ranges::sort(names, [](const std::string& lhs, const std::string& rhs) {
    return detail::MakeSortKey(false, lhs) < detail::MakeSortKey(false, rhs);
  });

  EXPECT_EQ(names, (std::vector<std::string> { "a0", "A1", "b0" }));
  // Same letters differing by case: the lowercase spelling comes first.
  EXPECT_LT(detail::MakeSortKey(false, "a"), detail::MakeSortKey(false, "A"));
}

NOLINT_TEST(FileBrowserHelpersTest, SortKeyOrdersNonAsciiBytesAfterAscii)
{
  // Bytes above 0x7F are negative as `char`; they must not sort first.
  EXPECT_LT(
    detail::MakeSortKey(false, "z"), detail::MakeSortKey(false, "\xC3\xA9"));
}

NOLINT_TEST(FileBrowserHelpersTest, TypeFilterWithoutFiltersMatchesEverything)
{
  EXPECT_TRUE(detail::MatchesTypeFilter({}, false, 0, ".txt"));
  EXPECT_TRUE(detail::MatchesTypeFilter({}, false, 0, ""));
}

NOLINT_TEST(FileBrowserHelpersTest, TypeFilterMatchesTheSelectedExtensionOnly)
{
  const std::vector<std::string> filters { ".txt", ".cpp" };

  EXPECT_TRUE(detail::MatchesTypeFilter(filters, false, 0, ".txt"));
  EXPECT_FALSE(detail::MatchesTypeFilter(filters, false, 0, ".cpp"));
  EXPECT_TRUE(detail::MatchesTypeFilter(filters, false, 1, ".cpp"));
  EXPECT_FALSE(detail::MatchesTypeFilter(filters, false, 1, ""));
}

NOLINT_TEST(FileBrowserHelpersTest, TypeFilterAllEntryMatchesEveryListedFilter)
{
  const std::vector<std::string> filters { ".txt,.cpp", ".txt", ".cpp" };

  EXPECT_TRUE(detail::MatchesTypeFilter(filters, true, 0, ".txt"));
  EXPECT_TRUE(detail::MatchesTypeFilter(filters, true, 0, ".cpp"));
  EXPECT_FALSE(detail::MatchesTypeFilter(filters, true, 0, ".png"));
}

NOLINT_TEST(FileBrowserHelpersTest, TypeFilterUniversalEntryMatchesAnyFile)
{
  const std::vector<std::string> filters { ".*" };

  EXPECT_TRUE(detail::MatchesTypeFilter(filters, false, 0, ".png"));
  EXPECT_TRUE(detail::MatchesTypeFilter(filters, false, 0, ""));
}

NOLINT_TEST(FileBrowserHelpersTest, TypeFilterOutOfRangeIndexMatchesEverything)
{
  const std::vector<std::string> filters { ".txt" };

  EXPECT_TRUE(detail::MatchesTypeFilter(filters, false, 7, ".png"));
}

NOLINT_TEST(FileBrowserFlagsTest, FlagsCombineAndTestIndependently)
{
  auto flags = FileBrowserFlags::kCloseOnEsc | FileBrowserFlags::kCreateNewDir;

  EXPECT_NE(flags & FileBrowserFlags::kCloseOnEsc, FileBrowserFlags::kNone);
  EXPECT_EQ(flags & FileBrowserFlags::kNoModal, FileBrowserFlags::kNone);

  flags &= ~FileBrowserFlags::kCloseOnEsc;
  EXPECT_EQ(flags, FileBrowserFlags::kCreateNewDir);
}

//=== Browser state ===------------------------------------------------------//

//! Provides a scratch directory tree that is removed after each test.
class FileBrowserTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    root_ = fs::weakly_canonical(fs::temp_directory_path())
      / ("oxygen-filebrowser-" + oxygen::Uuid::Generate().ToString());
    fs::create_directories(root_ / "dirA");
    fs::create_directories(root_ / "dirB");
    Touch(root_ / "alpha.txt");
    Touch(root_ / "Beta.TXT");
    Touch(root_ / "dirA" / "inner.cpp");
  }

  void TearDown() override
  {
    std::error_code ignored;
    fs::remove_all(root_, ignored);
  }

  [[nodiscard]] auto Root() const -> const fs::path& { return root_; }

private:
  static void Touch(const fs::path& path)
  {
    const std::ofstream file(path);
    ASSERT_TRUE(file.good());
  }

  fs::path root_;
};

NOLINT_TEST_F(FileBrowserTest, ConstructionEntersTheDefaultDirectory)
{
  const FileBrowser browser(FileBrowserFlags::kNone, Root());

  EXPECT_EQ(browser.GetDirectory(), Root());
  EXPECT_FALSE(browser.HasSelected());
  EXPECT_FALSE(browser.IsOpened());
}

NOLINT_TEST_F(FileBrowserTest, SetDirectoryEntersAnExistingDirectory)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());

  EXPECT_TRUE(browser.SetDirectory(Root() / "dirA"));

  EXPECT_EQ(browser.GetDirectory(), Root() / "dirA");
}

NOLINT_TEST_F(FileBrowserTest, SetDirectoryDropsTheTrailingSeparator)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());

  EXPECT_TRUE(browser.SetDirectory(Root() / "dirA" / ""));

  // Otherwise ".." from there would resolve to the same directory.
  EXPECT_EQ(browser.GetDirectory(), Root() / "dirA");
  EXPECT_EQ(browser.GetDirectory().parent_path(), Root());
}

NOLINT_TEST_F(FileBrowserTest, SetDirectoryResolvesDotDotSegments)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());

  EXPECT_TRUE(browser.SetDirectory(Root() / "dirA" / ".." / "dirB"));

  EXPECT_EQ(browser.GetDirectory(), Root() / "dirB");
}

NOLINT_TEST_F(
  FileBrowserTest, SetDirectoryKeepsTheCurrentOneWhenTargetIsMissing)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());
  ASSERT_TRUE(browser.SetDirectory(Root() / "dirA"));

  EXPECT_FALSE(browser.SetDirectory(Root() / "missing"));

  EXPECT_EQ(browser.GetDirectory(), Root() / "dirA");
}

NOLINT_TEST_F(
  FileBrowserTest, SetDirectoryOfAMissingDirectoryFailsEvenWhenSkipping)
{
  // kSkipItemsCausingError concerns entries, not the directory itself.
  FileBrowser browser(FileBrowserFlags::kSkipItemsCausingError, Root());

  EXPECT_FALSE(browser.SetDirectory(Root() / "missing"));

  EXPECT_EQ(browser.GetDirectory(), Root());
}

NOLINT_TEST_F(FileBrowserTest, SetDirectoryFallsBackToTheDefaultDirectory)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());
  ASSERT_TRUE(browser.SetDirectory(Root() / "dirB"));
  std::error_code ignored;
  fs::remove_all(Root() / "dirB", ignored);

  // Neither the target nor the current directory exists any more.
  EXPECT_FALSE(browser.SetDirectory(Root() / "gone"));

  EXPECT_EQ(browser.GetDirectory(), Root());
}

NOLINT_TEST_F(FileBrowserTest, NothingSelectedReportsTheBrowsedDirectory)
{
  FileBrowser browser(FileBrowserFlags::kSelectDirectory, Root());

  EXPECT_EQ(browser.GetSelected(), Root());
  EXPECT_EQ(browser.GetMultiSelected(), std::vector<fs::path> { Root() });
}

NOLINT_TEST_F(FileBrowserTest, OpenWithAPrefilledNameSelectsItWithoutConfirming)
{
  FileBrowser browser(FileBrowserFlags::kEnterNewFilename, Root());
  browser.SetInputName("saved.txt");

  browser.Open();

  EXPECT_FALSE(browser.HasSelected());
  EXPECT_EQ(browser.GetSelected(), Root() / "saved.txt");
}

NOLINT_TEST_F(FileBrowserTest, ClearSelectedForgetsThePrefilledName)
{
  FileBrowser browser(FileBrowserFlags::kEnterNewFilename, Root());
  browser.SetInputName("saved.txt");
  browser.Open();

  browser.ClearSelected();

  EXPECT_EQ(browser.GetSelected(), Root());
}

NOLINT_TEST_F(FileBrowserTest, PrefilledNameSurvivesAnUnchangedDirectoryReentry)
{
  FileBrowser browser(FileBrowserFlags::kEnterNewFilename, Root());
  browser.SetInputName("saved.txt");
  browser.Open();

  ASSERT_TRUE(browser.SetDirectory(Root()));

  EXPECT_EQ(browser.GetSelected(), Root() / "saved.txt");
}

NOLINT_TEST_F(FileBrowserTest, PrefilledNameFollowsTheUserIntoOtherDirectories)
{
  FileBrowser browser(FileBrowserFlags::kEnterNewFilename, Root());
  browser.SetInputName("saved.txt");
  browser.Open();

  // The input still holds the pre-filled name, so the selection follows.
  ASSERT_TRUE(browser.SetDirectory(Root() / "dirA"));

  EXPECT_EQ(browser.GetSelected(), Root() / "dirA" / "saved.txt");
}

NOLINT_TEST_F(FileBrowserTest, CloseClearsTheSelection)
{
  FileBrowser browser(FileBrowserFlags::kEnterNewFilename, Root());
  browser.SetInputName("saved.txt");
  browser.Open();

  browser.Close();

  EXPECT_FALSE(browser.HasSelected());
  EXPECT_EQ(browser.GetSelected(), Root());
}

NOLINT_TEST_F(FileBrowserTest, TypeFilterApiToleratesAnyIndexAndAnyOrder)
{
  FileBrowser browser(FileBrowserFlags::kNone, Root());

  browser.SetCurrentTypeFilterIndex(5);
  browser.SetTypeFilters({ ".txt", ".txt", ".cpp" });
  browser.SetCurrentTypeFilterIndex(99);
  browser.SetTypeFilters({});
  browser.SetCurrentTypeFilterIndex(3);

  EXPECT_EQ(browser.GetDirectory(), Root());
}

//=== Special members ===----------------------------------------------------//

NOLINT_TEST_F(FileBrowserTest, WindowNameCombinesTitleAndAUniqueSuffix)
{
  FileBrowser first(FileBrowserFlags::kNone, Root());
  FileBrowser second(FileBrowserFlags::kNone, Root());
  first.SetTitle("Pick a file");

  const std::string name = first.GetWindowName();

  EXPECT_TRUE(name.starts_with("Pick a file##filebrowser_"));
  EXPECT_NE(name, second.GetWindowName());
}

NOLINT_TEST_F(FileBrowserTest, CopyHasItsOwnWindowNameAndTheSameConfiguration)
{
  FileBrowser original(FileBrowserFlags::kEnterNewFilename, Root());
  original.SetTitle("Original");
  original.SetInputName("saved.txt");
  original.Open();
  ASSERT_TRUE(original.SetDirectory(Root() / "dirA"));

  FileBrowser copy(original);

  EXPECT_EQ(copy.GetDirectory(), original.GetDirectory());
  EXPECT_EQ(copy.GetSelected(), original.GetSelected());
  EXPECT_NE(copy.GetWindowName(), original.GetWindowName());
  EXPECT_TRUE(copy.GetWindowName().starts_with("Original##filebrowser_"));
}

NOLINT_TEST_F(FileBrowserTest, CopiesAreIndependent)
{
  FileBrowser original(FileBrowserFlags::kNone, Root());
  FileBrowser copy(original);

  ASSERT_TRUE(copy.SetDirectory(Root() / "dirB"));
  copy.SetTitle("Copy");

  EXPECT_EQ(original.GetDirectory(), Root());
  EXPECT_TRUE(original.GetWindowName().starts_with("file browser##"));
}

NOLINT_TEST_F(FileBrowserTest, CopyAssignmentKeepsTheTargetWindowName)
{
  FileBrowser source(FileBrowserFlags::kNone, Root());
  ASSERT_TRUE(source.SetDirectory(Root() / "dirA"));
  source.SetTitle("Source");
  FileBrowser target(FileBrowserFlags::kNone, Root());
  const std::string target_suffix
    = target.GetWindowName().substr(target.GetWindowName().find("##"));

  target = source;

  EXPECT_EQ(target.GetDirectory(), Root() / "dirA");
  EXPECT_EQ(target.GetWindowName(), "Source" + target_suffix);
  EXPECT_NE(target.GetWindowName(), source.GetWindowName());
}

NOLINT_TEST_F(FileBrowserTest, MoveTransfersStateAndGetsItsOwnWindowName)
{
  FileBrowser original(FileBrowserFlags::kEnterNewFilename, Root());
  original.SetTitle("Moved");
  original.SetInputName("saved.txt");
  original.Open();
  ASSERT_TRUE(original.SetDirectory(Root() / "dirA"));
  const std::string original_name = original.GetWindowName();

  FileBrowser moved(std::move(original));

  EXPECT_EQ(moved.GetDirectory(), Root() / "dirA");
  EXPECT_EQ(moved.GetSelected(), Root() / "dirA" / "saved.txt");
  EXPECT_NE(moved.GetWindowName(), original_name);
  EXPECT_TRUE(moved.GetWindowName().starts_with("Moved##filebrowser_"));
}

NOLINT_TEST_F(FileBrowserTest, MoveAssignmentTransfersState)
{
  FileBrowser source(FileBrowserFlags::kNone, Root());
  ASSERT_TRUE(source.SetDirectory(Root() / "dirB"));
  FileBrowser target(FileBrowserFlags::kNone, Root());

  target = std::move(source);

  EXPECT_EQ(target.GetDirectory(), Root() / "dirB");
}

} // namespace
