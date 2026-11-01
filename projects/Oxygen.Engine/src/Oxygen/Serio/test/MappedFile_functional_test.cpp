//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/MappedFile.h, Serio/MappedFile.cpp,
// Serio/Platform/*/MappedFile.cpp, Serio/Detail/GuardedCopy.h

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Support/DeviceFactories.h"

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Detail/GuardedCopy.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <errhandlingapi.h>
#  include <excpt.h>
#  include <memoryapi.h>
#endif

namespace {

using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::Error;
using oxygen::serio::MappedFile;
using oxygen::serio::WriteFileAtomically;
using oxygen::serio::test::Required;
using oxygen::serio::test::TempFiles;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;

//! Reads and views are in the device conformance suite; these cases cover
//! what only a mapping adds: its life across replacement of the file, and
//! the guarded copy that turns a failed page-in into an error.
class MappedFileTest : public ::testing::Test {
protected:
  const std::vector<std::byte> bytes_ = PatternBytes(10'000);
  TempFiles files_;
};

//! Mapping a missing file fails with kNotFound.
NOLINT_TEST_F(MappedFileTest, MissingFileIsNotFound)
{
  const auto mapped = MappedFile::Open(files_.Path("missing.bin"));

  ASSERT_FALSE(mapped);
  EXPECT_EQ(mapped.error(), Error::kNotFound);
}

//! An empty file maps to an empty view.
NOLINT_TEST_F(MappedFileTest, EmptyFileMapsEmptyView)
{
  const auto mapped = MappedFile::Open(files_.Write({}));

  ASSERT_TRUE(mapped);
  EXPECT_TRUE(mapped->Bytes().empty());
  EXPECT_EQ(mapped->Size()->get(), 0U);
}

//! Replacing the file atomically leaves the view on the complete version it
//! mapped; the next open maps the new version.
NOLINT_TEST_F(MappedFileTest, ReplacementKeepsMappedVersion)
{
  const auto path = files_.Write(bytes_);
  const auto replacement = PatternBytes(3000, PatternSeed::kSecond);
  const auto mapped = Required(MappedFile::Open(path), "map");

  const auto replaced = WriteFileAtomically(path, replacement);

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_THAT(mapped.Bytes(), BytesEqual(bytes_));
  EXPECT_THAT(
    Required(MappedFile::Open(path), "remap").Bytes(), BytesEqual(replacement));
}

//! A file another handle holds open for writing maps when the mapping
//! shares writes, and shows the bytes the file held when it was mapped.
NOLINT_TEST_F(MappedFileTest, MapsAFileAWriterHoldsWhenSharingWrites)
{
  const auto path = files_.Write(bytes_);
  auto writer
    = Required(oxygen::serio::NativeFile::Open(path,
                 {
                   .access = oxygen::serio::Access::kReadWrite,
                   .sharing = oxygen::serio::Sharing::kReadWriteDelete,
                 }),
      "open the writer");

  const auto mapped
    = MappedFile::Open(path, oxygen::serio::Sharing::kReadWriteDelete);

  ASSERT_TRUE(mapped) << mapped.error().message();
  EXPECT_THAT(mapped->Bytes(), BytesEqual(bytes_));
}

#ifdef OXYGEN_WINDOWS
//! By default a mapping refuses a file another handle writes: on Windows the
//! open fails with a sharing violation.
NOLINT_TEST_F(MappedFileTest, DefaultSharingRefusesAFileAWriterHolds)
{
  const auto path = files_.Write(bytes_);
  auto writer
    = Required(oxygen::serio::NativeFile::Open(path,
                 {
                   .access = oxygen::serio::Access::kReadWrite,
                   .sharing = oxygen::serio::Sharing::kReadWriteDelete,
                 }),
      "open the writer");

  const auto mapped = MappedFile::Open(path);

  ASSERT_FALSE(mapped);
  EXPECT_EQ(mapped.error(), Error::kSharingViolation);
}
#endif

//! Prefetch hints, including ranges past the end, leave reads unchanged.
NOLINT_TEST_F(MappedFileTest, PrefetchLeavesReadsUnchanged)
{
  const auto mapped = Required(MappedFile::Open(files_.Write(bytes_)), "map");
  auto out = std::vector<std::byte>(100);

  mapped.Prefetch(
    ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { 4096 } });
  mapped.Prefetch(
    ByteRange { .offset = ByteOffset { 9000 }, .count = ByteCount { 5000 } });
  mapped.Prefetch(
    ByteRange { .offset = ByteOffset { 20'000 }, .count = ByteCount { 1 } });
  const auto read = mapped.ReadAt(ByteOffset { 9000 }, out);

  ASSERT_TRUE(read);
  EXPECT_THAT(out, BytesEqual(std::span(bytes_).subspan(9000, 100)));
}

#ifdef OXYGEN_WINDOWS

//! Relabels an access violation at one address range as the in-page error a
//! failing disk raises, for the time it is installed. A mapped file cannot be
//! truncated on Windows, so this is how a test reaches the guarded copy's
//! failure path; the filter that runs is the product's own.
class InPageErrorInjector {
public:
  explicit InPageErrorInjector(const std::span<const std::byte> range)
    : handler_(AddVectoredExceptionHandler(1, &InPageErrorInjector::Relabel))
  {
    begin_ = std::bit_cast<std::uintptr_t>(range.data());
    end_ = begin_ + range.size();
  }

  ~InPageErrorInjector() { RemoveVectoredExceptionHandler(handler_); }

  InPageErrorInjector(const InPageErrorInjector&) = delete;
  auto operator=(const InPageErrorInjector&) -> InPageErrorInjector& = delete;
  InPageErrorInjector(InPageErrorInjector&&) = delete;
  auto operator=(InPageErrorInjector&&) -> InPageErrorInjector& = delete;

private:
  static auto CALLBACK Relabel(EXCEPTION_POINTERS* pointers) -> LONG
  {
    auto* const record = pointers->ExceptionRecord;
    const auto address
      = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && address >= begin_
      && address < end_) {
      record->ExceptionCode = EXCEPTION_IN_PAGE_ERROR;
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }

  static inline std::uintptr_t begin_ = 0;
  static inline std::uintptr_t end_ = 0;
  void* handler_ = nullptr;
};

//! A page that fails to page in under a guarded copy is reported, not fatal.
NOLINT_TEST(GuardedCopyTest, FailedPageInIsReported)
{
  constexpr auto kPage = size_t { 4096 };
  auto* const reserved = static_cast<std::byte*>(
    VirtualAlloc(nullptr, kPage, MEM_RESERVE, PAGE_NOACCESS));
  ASSERT_NE(reserved, nullptr);
  const auto release = oxygen::ScopeGuard(
    [reserved] noexcept -> void { VirtualFree(reserved, 0, MEM_RELEASE); });
  const auto injector = InPageErrorInjector(std::span(reserved, kPage));
  auto out = std::array<std::byte, 16> {};

  const auto copied
    = oxygen::serio::detail::GuardedCopy(out.data(), reserved, out.size());

  EXPECT_FALSE(copied);
}

#else

//! A mapped page lost to a truncated file fails ReadAt with kIo instead of
//! killing the process.
NOLINT_TEST_F(MappedFileTest, FailedPageInIsIo)
{
  const auto path = files_.Write(bytes_);
  const auto mapped = Required(MappedFile::Open(path), "map");
  std::filesystem::resize_file(path, 0);
  auto out = std::vector<std::byte>(16);

  const auto read = mapped.ReadAt(ByteOffset { 8192 }, out);

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kIo);
}

#endif

} // namespace
