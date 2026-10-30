//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/NativeFile.h, Serio/Platform/*/NativeFile.cpp,
// Serio/Platform/*/SystemErrors.cpp

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Support/DeviceFactories.h"

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::Access;
using oxygen::serio::ByteBuffer;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::Creation;
using oxygen::serio::Error;
using oxygen::serio::kIoAlignment;
using oxygen::serio::NativeFile;
using oxygen::serio::NativeHandle;
using oxygen::serio::ReadExactlyAt;
using oxygen::serio::Sharing;
using oxygen::serio::WriteFileAtomically;
using oxygen::serio::test::Required;
using oxygen::serio::test::TempFiles;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;

//! Positional IO is in the device conformance suite; these cases cover what
//! only a file adds: opening, creation, sharing and OS errors, compared
//! through `serio::Error` conditions so they hold on every platform.
class NativeFileTest : public ::testing::Test {
protected:
  auto ReadAll(const NativeFile& file) -> std::vector<std::byte>
  {
    auto bytes = std::vector<std::byte>(
      static_cast<size_t>(Required(file.Size(), "Size").get()));
    Required(ReadExactlyAt(file, ByteOffset { 0 }, bytes), "read");
    return bytes;
  }

  [[nodiscard]] auto Bytes() const -> const std::vector<std::byte>&
  {
    return bytes_;
  }
  auto Files() -> TempFiles& { return files_; }

private:
  const std::vector<std::byte> bytes_ = PatternBytes(1000);
  TempFiles files_;
};

//! Opening a missing file fails with kNotFound.
NOLINT_TEST_F(NativeFileTest, OpenMissingIsNotFound)
{
  const auto file = NativeFile::Open(Files().Path("missing.bin"), {});

  ASSERT_FALSE(file);
  EXPECT_EQ(file.error(), Error::kNotFound);
}

//! Creating a new file where one exists fails, and leaves it unchanged.
NOLINT_TEST_F(NativeFileTest, CreateNewRefusesExistingFile)
{
  const auto path = Files().Write(Bytes());

  const auto file = NativeFile::Open(
    path, { .access = Access::kWrite, .creation = Creation::kCreateNew });

  ASSERT_FALSE(file);
  EXPECT_EQ(file.error(), std::errc::file_exists);
  EXPECT_THAT(ReadAll(Required(NativeFile::Open(path, {}), "reopen")),
    BytesEqual(Bytes()));
}

//! Creating a file always truncates one that exists.
NOLINT_TEST_F(NativeFileTest, CreateAlwaysTruncates)
{
  const auto path = Files().Write(Bytes());

  const auto file = NativeFile::Open(path,
    { .access = Access::kReadWrite, .creation = Creation::kCreateAlways });

  ASSERT_TRUE(file);
  EXPECT_EQ(file->Size()->get(), 0U);
}

//! Opening always creates a missing file and keeps an existing one.
NOLINT_TEST_F(NativeFileTest, OpenAlwaysCreatesOrKeeps)
{
  const auto existing = Files().Write(Bytes());
  const auto missing = Files().Path("created.bin");
  const auto spec = oxygen::serio::OpenSpec {
    .access = Access::kReadWrite,
    .creation = Creation::kOpenAlways,
  };

  const auto kept = NativeFile::Open(existing, spec);
  const auto created = NativeFile::Open(missing, spec);

  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->Size()->get(), Bytes().size());
  ASSERT_TRUE(created);
  EXPECT_EQ(created->Size()->get(), 0U);
}

//! A file opened for reading refuses writes with kAccessDenied.
NOLINT_TEST_F(NativeFileTest, ReadOnlyFileRefusesWrites)
{
  auto file = Required(NativeFile::Open(Files().Write(Bytes()), {}), "open");

  const auto written = file.WriteAt(ByteOffset { 0 }, Bytes());

  ASSERT_FALSE(written);
  EXPECT_EQ(written.error(), Error::kAccessDenied);
}

//! A file opened without sharing refuses a second open with
//! kSharingViolation. POSIX has no sharing modes.
NOLINT_TEST_F(NativeFileTest, ExclusiveOpenRefusesOthers)
{
#ifdef OXYGEN_WINDOWS
  const auto path = Files().Write(Bytes());
  const auto exclusive = Required(
    NativeFile::Open(path, { .sharing = Sharing::kNone }), "exclusive open");

  const auto second = NativeFile::Open(path, {});

  ASSERT_FALSE(second);
  EXPECT_EQ(second.error(), Error::kSharingViolation);
#else
  GTEST_SKIP() << "POSIX does not enforce sharing modes";
#endif
}

//! The default sharing lets the file be replaced atomically while it is open;
//! the open file keeps reading the bytes it opened.
NOLINT_TEST_F(NativeFileTest, DefaultSharingAllowsReplacement)
{
  const auto path = Files().Write(Bytes());
  const auto replacement = PatternBytes(500, PatternSeed::kSecond);
  const auto file = Required(NativeFile::Open(path, {}), "open");

  const auto replaced = WriteFileAtomically(path, replacement);

  ASSERT_TRUE(replaced);
  EXPECT_THAT(ReadAll(file), BytesEqual(Bytes()));
  EXPECT_THAT(ReadAll(Required(NativeFile::Open(path, {}), "reopen")),
    BytesEqual(replacement));
}

//! A path longer than the classic Windows limit opens through its logical
//! spelling, which the file keeps for diagnostics.
NOLINT_TEST_F(NativeFileTest, LongPathOpensAndKeepsLogicalPath)
{
  const auto path = Files().Path(
    std::string(150, 'a') + "/" + std::string(150, 'b') + "/long.bin");
  ASSERT_GT(path.native().size(), 260U);

  const auto file = NativeFile::Open(
    path, { .access = Access::kReadWrite, .creation = Creation::kCreateNew });

  ASSERT_TRUE(file) << file.error().message();
  EXPECT_EQ(file->Path(), path);
}

//! An unbuffered file reads sector-aligned ranges into an aligned buffer.
NOLINT_TEST_F(NativeFileTest, UnbufferedReadsAlignedRanges)
{
  const auto content = PatternBytes(4 * kIoAlignment);
  const auto file = Required(
    NativeFile::Open(Files().Write(content), { .unbuffered = true }), "open");
  auto buffer
    = Required(ByteBuffer::Allocate(ByteCount { kIoAlignment }), "allocate");

  const auto read
    = ReadExactlyAt(file, ByteOffset { kIoAlignment }, buffer.Bytes());

  ASSERT_TRUE(read) << read.error().message();
  EXPECT_THAT(buffer.Bytes(),
    BytesEqual(std::span(content).subspan(kIoAlignment, kIoAlignment)));
}

//! Moving a file moves its handle; the source no longer holds one.
NOLINT_TEST_F(NativeFileTest, MoveTransfersHandle)
{
  auto source = Required(NativeFile::Open(Files().Write(Bytes()), {}), "open");
  const auto handle = source.Handle();

  const auto target = std::move(source);

  EXPECT_EQ(target.Handle(), handle);
  EXPECT_EQ(
    source.Handle(), NativeHandle::kInvalid); // NOLINT(*-use-after-move)
  EXPECT_THAT(ReadAll(target), BytesEqual(Bytes()));
}

} // namespace
