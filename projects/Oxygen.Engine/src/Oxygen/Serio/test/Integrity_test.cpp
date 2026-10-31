//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Serio/Integrity.h

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::base::Crc32;
using oxygen::base::Sha256;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::Error;
using oxygen::serio::HashingReadStream;
using oxygen::serio::HashingWriteStream;
using oxygen::serio::MemoryFile;
using oxygen::serio::MemoryStream;
using oxygen::serio::ReadOnlyMemoryStream;
using oxygen::serio::Region;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;

static_assert(oxygen::serio::IncrementalHash<Crc32>);
static_assert(oxygen::serio::IncrementalHash<Sha256>);
static_assert(oxygen::serio::PatchableOutputStream<
  HashingWriteStream<MemoryStream, Crc32>>);
static_assert(
  oxygen::serio::InputStream<HashingReadStream<ReadOnlyMemoryStream, Crc32>>);

//! More than two hash buffers, so a device is hashed in several pieces.
constexpr auto kLargeSize = size_t { 2'621'440 } + 17;

//! The bytes of `bytes` outside `skip`.
auto Without(const std::span<const std::byte> bytes, const ByteRange skip)
  -> std::vector<std::byte>
{
  const auto start = static_cast<std::ptrdiff_t>(skip.offset.get());
  auto kept = std::vector<std::byte>(bytes.begin(), bytes.begin() + start);
  kept.insert(kept.end(),
    bytes.begin() + static_cast<std::ptrdiff_t>(skip.End().get()), bytes.end());
  return kept;
}

//! Hashing a device in pieces gives the digests of all its bytes at once.
NOLINT_TEST(IntegrityTest, Hash_ReadsADeviceInPiecesOnce)
{
  const auto bytes = PatternBytes(kLargeSize);
  const auto file = MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(file);

  const auto digests = oxygen::serio::Hash<Crc32, Sha256>(*file);

  ASSERT_HAS_VALUE(digests);
  EXPECT_EQ(std::get<0>(*digests), oxygen::base::ComputeCrc32(bytes));
  EXPECT_EQ(std::get<1>(*digests), oxygen::base::ComputeSha256(bytes));
}

//! A region hashes only its own bytes.
NOLINT_TEST(IntegrityTest, ComputeSha256_HashesARegionOnly)
{
  const auto bytes = PatternBytes(4096);
  const auto file = MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(file);
  const auto region = Region<MemoryFile>::Make(*file,
    ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 1000 } });
  ASSERT_HAS_VALUE(region);

  const auto sha = oxygen::serio::ComputeSha256(*region);
  const auto crc = oxygen::serio::ComputeCrc32(*region);

  const auto inside = std::span(bytes).subspan(100, 1000);
  ASSERT_HAS_VALUE(sha);
  ASSERT_HAS_VALUE(crc);
  EXPECT_EQ(*sha, oxygen::base::ComputeSha256(inside));
  EXPECT_EQ(*crc, oxygen::base::ComputeCrc32(inside));
}

//! A device hashed in pieces leaves out a skipped field that spans two of
//! its pieces.
NOLINT_TEST(IntegrityTest, ComputeCrc32_LeavesOutASkippedField)
{
  const auto bytes = PatternBytes(kLargeSize);
  const auto file = MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(file);
  const auto field = ByteRange {
    .offset = ByteOffset { oxygen::serio::kHashBufferSize.get() - 2 },
    .count = ByteCount { 4 },
  };

  const auto crc = oxygen::serio::ComputeCrc32(*file, std::span { &field, 1 });
  const auto sha = oxygen::serio::ComputeSha256(*file, std::span { &field, 1 });

  const auto kept = Without(bytes, field);
  ASSERT_HAS_VALUE(crc);
  ASSERT_HAS_VALUE(sha);
  EXPECT_EQ(*crc, oxygen::base::ComputeCrc32(kept));
  EXPECT_EQ(*sha, oxygen::base::ComputeSha256(kept));
}

//! An empty device hashes to the digests of no bytes.
NOLINT_TEST(IntegrityTest, ComputeSha256_OfAnEmptyDeviceIsTheEmptyDigest)
{
  const auto file = MemoryFile::FromBytes({});
  ASSERT_HAS_VALUE(file);

  const auto sha = oxygen::serio::ComputeSha256(*file);

  ASSERT_HAS_VALUE(sha);
  EXPECT_EQ(*sha, oxygen::base::ComputeSha256({}));
}

//! Bytes written in pieces are hashed once, except a skipped field, which a
//! patch may then fill.
NOLINT_TEST(IntegrityTest, HashingWriteStream_SkipsAndPatchesItsField)
{
  const auto bytes = PatternBytes(1000);
  const auto field
    = ByteRange { .offset = ByteOffset { 500 }, .count = ByteCount { 4 } };
  auto stream = MemoryStream {};
  auto hashing = HashingWriteStream<MemoryStream, Crc32, Sha256> { stream,
    std::span { &field, 1 } };

  // Pieces that end inside the field and start after it.
  ASSERT_TRUE(hashing.Write(std::span(bytes).first(502)));
  ASSERT_TRUE(hashing.Write(std::span(bytes).subspan(502, 300)));
  ASSERT_TRUE(hashing.Write(std::span(bytes).subspan(802)));
  const auto [crc, sha] = hashing.Digests();
  const auto value = std::array<std::byte, 4> {};
  const auto patched = hashing.PatchAt(field.offset, value);
  const auto outside = hashing.PatchAt(ByteOffset { 499 }, value);

  const auto kept = Without(bytes, field);
  EXPECT_EQ(crc, oxygen::base::ComputeCrc32(kept));
  EXPECT_EQ(sha, oxygen::base::ComputeSha256(kept));
  EXPECT_TRUE(patched);
  ASSERT_FALSE(outside);
  EXPECT_EQ(outside.error(), Error::kOutOfBounds);
  EXPECT_EQ(stream.Data().size(), bytes.size());
}

//! Every byte read is hashed once, including bytes a seek forward passes.
NOLINT_TEST(IntegrityTest, HashingReadStream_HashesBytesReadAndPassed)
{
  const auto bytes = PatternBytes(10000);
  auto stream = ReadOnlyMemoryStream { bytes };
  auto hashing = HashingReadStream<ReadOnlyMemoryStream, Crc32> { stream };
  auto head = std::array<std::byte, 100> {};

  ASSERT_TRUE(oxygen::serio::ReadExactly(hashing, head));
  ASSERT_TRUE(hashing.Seek(ByteOffset { 9000 }));
  auto tail = std::vector<std::byte>(1000);
  ASSERT_TRUE(oxygen::serio::ReadExactly(hashing, std::span { tail }));
  const auto back = hashing.Seek(ByteOffset { 0 });

  EXPECT_EQ(std::get<0>(hashing.Digests()), oxygen::base::ComputeCrc32(bytes));
  ASSERT_FALSE(back);
  EXPECT_EQ(back.error(), std::errc::not_supported);
}

//! A verified read fills the destination and accepts the expected digest.
NOLINT_TEST(IntegrityTest, VerifiedRead_FillsTheDestination)
{
  const auto bytes = PatternBytes(kLargeSize);
  const auto file = MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(file);
  auto out = std::vector<std::byte>(bytes.size());

  const auto read = oxygen::serio::VerifiedRead(
    *file, oxygen::base::ComputeSha256(bytes), std::span { out });

  EXPECT_TRUE(read);
  EXPECT_THAT(out, BytesEqual(bytes));
}

//! A verified read refuses bytes whose digest differs, and a destination of
//! another size.
NOLINT_TEST(IntegrityTest, VerifiedRead_RefusesAnotherDigestOrSize)
{
  const auto bytes = PatternBytes(1000);
  const auto file = MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(file);
  auto expected = oxygen::base::ComputeSha256(bytes);
  expected[0] = static_cast<uint8_t>(expected[0] ^ 1U);
  auto out = std::vector<std::byte>(bytes.size());
  auto short_out = std::vector<std::byte>(bytes.size() - 1);

  const auto mismatch
    = oxygen::serio::VerifiedRead(*file, expected, std::span { out });
  const auto wrong_size = oxygen::serio::VerifiedRead(
    *file, oxygen::base::ComputeSha256(bytes), std::span { short_out });

  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error(), Error::kDigestMismatch);
  ASSERT_FALSE(wrong_size);
  EXPECT_EQ(wrong_size.error(), Error::kOutOfBounds);
}

} // namespace
