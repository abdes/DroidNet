//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Testing/GTest.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep
#endif

namespace {

//! Reads a file through a handle that stays open, sharing it as readers of
//! published files must: reads, writes and deletion.
class SharingReader final {
public:
  explicit SharingReader(const std::filesystem::path& path)
#ifdef OXYGEN_WINDOWS
    : handle_(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
#else
    : input_(path, std::ios::binary)
#endif
  {
  }
  ~SharingReader()
  {
#ifdef OXYGEN_WINDOWS
    if (IsOpen()) {
      CloseHandle(handle_);
    }
#endif
  }
  SharingReader(const SharingReader&) = delete;
  auto operator=(const SharingReader&) -> SharingReader& = delete;
  SharingReader(SharingReader&&) = delete;
  auto operator=(SharingReader&&) -> SharingReader& = delete;

  [[nodiscard]] auto IsOpen() const -> bool
  {
#ifdef OXYGEN_WINDOWS
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return input_.is_open();
#endif
  }

  auto ReadAll() -> std::string
  {
#ifdef OXYGEN_WINDOWS
    auto text = std::string {};
    auto chunk = std::vector<char>(256);
    DWORD read = 0;
    while (ReadFile(handle_, chunk.data(), static_cast<DWORD>(chunk.size()),
             &read, nullptr)
        != FALSE
      && read != 0U) {
      text.append(chunk.data(), read);
    }
    return text;
#else
    return { std::istreambuf_iterator<char>(input_), {} };
#endif
  }

private:
#ifdef OXYGEN_WINDOWS
  HANDLE handle_;
#else
  std::ifstream input_;
#endif
};

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
  static auto Text(const std::vector<std::byte>& bytes) -> std::string
  {
    auto text = std::string(bytes.size(), '\0');
    std::ranges::transform(bytes, text.begin(),
      [](const std::byte byte) -> char { return static_cast<char>(byte); });
    return text;
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

//! A reader of the published file never blocks its replacement: the
//! publication succeeds, the reader keeps the bytes it opened, and new reads
//! see the new bytes.
NOLINT_TEST_F(AtomicFileTest, ReplacesDestinationWhileASharingReaderHoldsIt)
{
  const auto path = root_ / "container.index.bin";
  const std::string before = "published-before";
  const std::string after = "published-after-with-more-bytes";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));

  auto reader = SharingReader(path);
  ASSERT_TRUE(reader.IsOpen());
  const auto replaced = oxygen::serio::WriteFileAtomically(path, Bytes(after));

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(reader.ReadAll(), before);
  const auto current = oxygen::serio::ReadPublishedFile(path);
  ASSERT_TRUE(current) << current.error().message();
  EXPECT_EQ(Text(*current), after);
}

//! The destination is named exactly as asked, whatever its length: the
//! replacement never reads past the name or names a different file.
NOLINT_TEST_F(AtomicFileTest, ReplacesDestinationsOfEveryNameLength)
{
  auto name = std::string("i");
  for (auto length = 1; length <= 8; ++length, name += 'n') {
    const auto path = root_ / name;
    const std::string before = "before";
    const std::string after = "after";
    ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
    const auto replaced
      = oxygen::serio::WriteFileAtomically(path, Bytes(after));

    ASSERT_TRUE(replaced) << name << ": " << replaced.error().message();
    EXPECT_EQ(Read(path), after) << name;
  }
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root_),
              std::filesystem::directory_iterator {}),
    8);
}

NOLINT_TEST_F(AtomicFileTest, ReadPublishedFileReturnsEveryPublishedByte)
{
  const auto path = root_ / "record.json";
  const auto text = std::string(70000, 'x') + "tail";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(text)));

  const auto bytes = oxygen::serio::ReadPublishedFile(path);

  ASSERT_TRUE(bytes) << bytes.error().message();
  EXPECT_EQ(Text(*bytes), text);
}

NOLINT_TEST_F(AtomicFileTest, ReadPublishedFileReportsAMissingFile)
{
  EXPECT_FALSE(oxygen::serio::ReadPublishedFile(root_ / "missing.bin"));
}

} // namespace
