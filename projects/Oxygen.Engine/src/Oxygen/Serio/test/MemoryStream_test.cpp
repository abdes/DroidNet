//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/MemoryStream.h

#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::MemoryStream;
using oxygen::serio::ReadExactly;
using oxygen::serio::ReadOnlyMemoryStream;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;

//! Input and output stream contracts are in the stream conformance suites;
//! these cases cover what the memory streams add: one cursor for reads and
//! writes, and owning or viewing their bytes.
class MemoryStreamTest : public ::testing::Test {
protected:
  const std::vector<std::byte> bytes_ = PatternBytes(200);
};

//! Written bytes read back after seeking to them.
NOLINT_TEST_F(MemoryStreamTest, ReadsBackWrittenBytes)
{
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Write(bytes_));
  auto out = std::vector<std::byte>(bytes_.size());

  ASSERT_TRUE(stream.Seek(ByteOffset { 0 }));
  ASSERT_TRUE(ReadExactly(stream, out));

  EXPECT_THAT(out, BytesEqual(bytes_));
}

//! A write after seeking back overwrites, and extends past the old end.
NOLINT_TEST_F(MemoryStreamTest, WriteAfterSeekOverwritesAndExtends)
{
  const auto patch = PatternBytes(20, PatternSeed::kSecond);
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Write(bytes_));

  ASSERT_TRUE(stream.Seek(ByteOffset { 190 }));
  ASSERT_TRUE(stream.Write(patch));

  auto expected = std::vector(bytes_.begin(), bytes_.begin() + 190);
  expected.insert(expected.end(), patch.begin(), patch.end());
  EXPECT_EQ(stream.Size().get(), 210U);
  EXPECT_THAT(stream.Data(), BytesEqual(expected));
}

//! PatchAt reaches only up to the cursor, even when bytes exist beyond it.
NOLINT_TEST_F(MemoryStreamTest, PatchAtStopsAtCursor)
{
  const auto patch = PatternBytes(8, PatternSeed::kSecond);
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Write(bytes_));
  ASSERT_TRUE(stream.Seek(ByteOffset { 50 }));

  const auto before = stream.PatchAt(ByteOffset { 42 }, patch);
  const auto beyond = stream.PatchAt(ByteOffset { 43 }, patch);

  EXPECT_TRUE(before);
  EXPECT_FALSE(beyond);
  EXPECT_EQ(stream.Position().get(), 50U);
}

//! Take moves the bytes out without a copy and empties the stream.
NOLINT_TEST_F(MemoryStreamTest, TakeMovesBytesOut)
{
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Write(bytes_));
  const auto* const data = stream.Data().data();

  const auto taken = stream.Take();

  EXPECT_EQ(taken.Bytes().data(), data);
  EXPECT_THAT(taken.Bytes(), BytesEqual(bytes_));
  EXPECT_EQ(stream.Size().get(), 0U);
  EXPECT_EQ(stream.Position().get(), 0U);
}

//! Clear empties and rewinds the stream, which stays usable.
NOLINT_TEST_F(MemoryStreamTest, ClearEmptiesAndRewinds)
{
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Write(bytes_));

  stream.Clear();
  ASSERT_TRUE(stream.Write(std::span(bytes_).first(3)));

  EXPECT_THAT(stream.Data(), BytesEqual(std::span(bytes_).first(3)));
}

//! Reserve keeps the size; writes up to the reservation keep the storage.
NOLINT_TEST_F(MemoryStreamTest, ReserveKeepsSizeAndStorage)
{
  auto stream = MemoryStream {};
  ASSERT_TRUE(stream.Reserve(ByteCount { 4096 }));
  ASSERT_TRUE(stream.Write(std::span(bytes_).first(1)));
  const auto* const data = stream.Data().data();

  ASSERT_TRUE(stream.Write(std::span(bytes_).subspan(1)));

  EXPECT_EQ(stream.Data().data(), data);
  EXPECT_EQ(stream.Size().get(), bytes_.size());
}

//! A read-only stream views its caller's bytes without copying them.
NOLINT_TEST_F(MemoryStreamTest, ReadOnlyStreamViewsCallerBytes)
{
  const auto stream = ReadOnlyMemoryStream(bytes_);

  EXPECT_EQ(stream.Data().data(), bytes_.data());
  EXPECT_EQ(stream.Size().get(), bytes_.size());
}

} // namespace
