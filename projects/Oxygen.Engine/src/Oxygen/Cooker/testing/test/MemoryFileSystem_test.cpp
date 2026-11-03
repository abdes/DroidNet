//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::lc::RootEntry;
using oxygen::content::lc::RootEntryKind;
using oxygen::cooker::testing::MemoryFileSystem;

//! A folder lists every folder and file below it, by its path below it, as
//! the real storage does: each file with its size, nothing outside it.
NOLINT_TEST(MemoryFileSystemTest, ListNamesEverythingBelowTheFolder)
{
  // Arrange
  auto files = MemoryFileSystem {};
  files.AddFolder("/root");
  files.AddFolder("/root/Materials");
  files.AddFile("/root/container.index.bin", std::vector<std::byte>(3));
  files.AddFile("/root/Materials/A.omat", std::vector<std::byte>(5));
  files.AddFile("/rootless/B.omat", std::vector<std::byte>(1));

  // Act
  const auto listed = files.List("/root");

  // Assert
  ASSERT_HAS_VALUE(listed);
  auto entries = *listed;
  std::ranges::sort(entries, {}, &RootEntry::relative_path);
  EXPECT_EQ(entries,
    (std::vector<RootEntry> {
      {
        .relative_path = "Materials",
        .kind = RootEntryKind::kFolder,
        .size = 0U,
      },
      {
        .relative_path = "Materials/A.omat",
        .kind = RootEntryKind::kFile,
        .size = 5U,
      },
      {
        .relative_path = "container.index.bin",
        .kind = RootEntryKind::kFile,
        .size = 3U,
      },
    }));
}

//! A folder that does not exist cannot be listed.
NOLINT_TEST(MemoryFileSystemTest, ListOfAMissingFolderFails)
{
  // Arrange
  const auto files = MemoryFileSystem {};

  // Act / Assert
  EXPECT_FALSE(files.List("/missing").has_value());
}

} // namespace
