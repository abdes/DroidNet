//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooked/CookerStorage.cpp (ReadWholeFile)

#include <string>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace cooked = oxygen::content::cooked;
namespace serio = oxygen::serio;

constexpr auto kLimit = serio::ByteCount { 64 };

//! A file exactly at the limit is read whole.
NOLINT_TEST(CookerStorageTest, ReadWholeFileReadsAFileAtTheLimit)
{
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  storage.AddFile("root/at.bin", std::string(kLimit.get(), 'a'));

  const auto bytes = cooked::ReadWholeFile(storage, "root/at.bin", kLimit);

  ASSERT_HAS_VALUE(bytes);
  EXPECT_EQ(bytes->size(), kLimit.get());
}

//! A file one byte past the limit is refused, not clipped.
NOLINT_TEST(CookerStorageTest, ReadWholeFileRefusesAFilePastTheLimit)
{
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  storage.AddFile("root/past.bin", std::string(kLimit.get() + 1U, 'a'));

  const auto bytes = cooked::ReadWholeFile(storage, "root/past.bin", kLimit);

  ASSERT_FALSE(bytes);
  EXPECT_EQ(bytes.error(), serio::Error::kTooLarge);
}

} // namespace
