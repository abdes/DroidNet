//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Serio/ByteReader.h

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

// The record has external linkage, so its `kIsRecord` specialization does too.
namespace byte_reader_test {

#pragma pack(push, 1)
struct Header {
  uint16_t version;
  float scale;
  uint32_t name_size;
};
#pragma pack(pop)

} // namespace byte_reader_test

// clang-format off
namespace oxygen::serio {
template <> inline constexpr bool kIsRecord<byte_reader_test::Header> = true;
} // namespace oxygen::serio
// clang-format on

namespace {

using byte_reader_test::Header;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteReader;
using oxygen::serio::Error;

//! Parses a header, a name and a trailing value out of one buffer.
class ByteReaderTest : public ::testing::Test {
protected:
  static constexpr std::string_view kName = "texture";
  static constexpr uint32_t kTrailer = 0xA5A5'0001U;

  ByteReaderTest()
  {
    const auto header = Header {
      .version = 3,
      .scale = 0.5F,
      .name_size = static_cast<uint32_t>(kName.size()),
    };
    auto out = std::span<std::byte> { bytes_ };
    out = Put(out, std::as_bytes(std::span { &header, 1 }));
    out = Put(out, std::as_bytes(std::span { kName }));
    Put(out, std::as_bytes(std::span { &kTrailer, 1 }));
  }

  //! Copies `in` to the front of `out` and returns the rest of `out`.
  static auto Put(const std::span<std::byte> out,
    const std::span<const std::byte> in) -> std::span<std::byte>
  {
    std::ranges::copy(in, out.begin());
    return out.subspan(in.size());
  }

  [[nodiscard]] auto Bytes() const -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  std::array<std::byte, sizeof(Header) + kName.size() + sizeof(uint32_t)>
    bytes_ {};
};

//! Records and values are copied out; names are views of the bytes.
NOLINT_TEST_F(ByteReaderTest, ParsesRecordNameAndValue)
{
  auto reader = ByteReader { Bytes() };

  const auto header = reader.Read<Header>();
  ASSERT_HAS_VALUE(header);
  const auto name = reader.ReadStringView(ByteCount { header->name_size });
  const auto trailer = reader.Read<uint32_t>();

  EXPECT_EQ(header->version, 3U);
  EXPECT_FLOAT_EQ(header->scale, 0.5F);
  ASSERT_HAS_VALUE(name);
  EXPECT_EQ(*name, kName);
  EXPECT_EQ(static_cast<const void*>(name->data()),
    static_cast<const void*>(Bytes().subspan(sizeof(Header)).data()));
  ASSERT_HAS_VALUE(trailer);
  EXPECT_EQ(*trailer, kTrailer);
  EXPECT_EQ(reader.Remaining().get(), 0U);
}

//! A read past the end fails with kEndOfData and leaves the cursor in place.
NOLINT_TEST_F(ByteReaderTest, ReadPastEnd_FailsAndKeepsCursor)
{
  auto reader = ByteReader { Bytes() };
  ASSERT_TRUE(reader.Seek(ByteOffset { Bytes().size() - 2 }));

  const auto value = reader.Read<uint32_t>();
  const auto bytes = reader.ReadBytes(ByteCount { 3 });

  ASSERT_FALSE(value);
  EXPECT_EQ(value.error(), Error::kEndOfData);
  ASSERT_FALSE(bytes);
  EXPECT_EQ(bytes.error(), Error::kEndOfData);
  EXPECT_EQ(reader.Position().get(), Bytes().size() - 2);
}

//! Seeking and skipping stop at the end.
NOLINT_TEST_F(ByteReaderTest, SeekAndSkip_StopAtEnd)
{
  auto reader = ByteReader { Bytes() };

  EXPECT_TRUE(reader.Seek(ByteOffset { Bytes().size() }));
  const auto past_seek = reader.Seek(ByteOffset { Bytes().size() + 1 });
  ASSERT_TRUE(reader.Seek(ByteOffset { 0 }));
  const auto past_skip = reader.Skip(ByteCount { Bytes().size() + 1 });

  ASSERT_FALSE(past_seek);
  EXPECT_EQ(past_seek.error(), Error::kOutOfBounds);
  ASSERT_FALSE(past_skip);
  EXPECT_EQ(past_skip.error(), Error::kOutOfBounds);
  EXPECT_EQ(reader.Position().get(), 0U);
}

//! Alignment is relative to the start of the bytes, and refuses non-powers.
NOLINT_TEST_F(ByteReaderTest, AlignTo_IsRelativeToStart)
{
  auto reader = ByteReader { Bytes() };
  ASSERT_TRUE(reader.Skip(ByteCount { 1 }));

  ASSERT_TRUE(reader.AlignTo(4));
  const auto invalid = reader.AlignTo(3);

  EXPECT_EQ(reader.Position().get(), 4U);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error(), std::errc::invalid_argument);
}

} // namespace
