//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <system_error>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Testing/GTest.h>

namespace {

class AtomicFileTest : public testing::Test {
protected:
  void SetUp() override
  {
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen-atomic-" + oxygen::Uuid::Generate().ToString());
    std::filesystem::create_directory(root_);
  }
  void TearDown() override
  {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }
  static auto Bytes(const std::string& text) -> std::span<const std::byte>
  {
    return std::as_bytes(std::span(text.data(), text.size()));
  }
  static auto Read(const std::filesystem::path& path) -> std::string
  {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), {} };
  }
  std::filesystem::path root_;
};

NOLINT_TEST_F(AtomicFileTest, ReplacesCompleteBytesAndLeavesNoTemporaryFiles)
{
  const auto path = root_ / "record.json";
  const std::string before = "old-record";
  const std::string after = "new-record-with-more-bytes";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(after)));
  EXPECT_EQ(Read(path), after);
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root_),
              std::filesystem::directory_iterator {}),
    1);
}

NOLINT_TEST_F(AtomicFileTest, RefusesDirectoryDestinationWithoutChangingIt)
{
  const auto directory = root_ / "record";
  std::filesystem::create_directory(directory);
  const std::string data = "value";
  EXPECT_FALSE(oxygen::serio::WriteFileAtomically(directory, Bytes(data)));
  EXPECT_TRUE(std::filesystem::is_directory(directory));
  EXPECT_TRUE(std::filesystem::is_empty(directory));
}

NOLINT_TEST_F(AtomicFileTest, MissingParentDoesNotCreatePartialOutput)
{
  const auto path = root_ / "missing" / "record.json";
  const std::string data = "value";
  EXPECT_FALSE(oxygen::serio::WriteFileAtomically(path, Bytes(data)));
  EXPECT_FALSE(std::filesystem::exists(path));
}

} // namespace
