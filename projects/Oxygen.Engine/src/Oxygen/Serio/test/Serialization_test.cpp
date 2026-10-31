//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Serio/Record.h, Serio/Reader.h, Serio/Writer.h

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

// Records have external linkage, so their `kIsRecord` specializations do too.
namespace serialization_test {

#pragma pack(push, 1)
//! A packed record with floating-point members, as file formats declare them.
struct PackedRecord {
  uint8_t kind;
  uint32_t count;
  float scale;
  std::array<float, 3> position;
};
#pragma pack(pop)

//! A naturally aligned record without padding.
struct AlignedRecord {
  uint32_t id;
  uint32_t flags;
  uint64_t size;
};

} // namespace serialization_test

// clang-format off
namespace oxygen::serio {
template <> inline constexpr bool kIsRecord<serialization_test::PackedRecord> = true;
template <> inline constexpr bool kIsRecord<serialization_test::AlignedRecord> = true;
} // namespace oxygen::serio
// clang-format on

namespace {

using oxygen::serio::ByteOffset;
using oxygen::serio::Error;
using oxygen::serio::MemoryStream;
using oxygen::serio::Reader;
using oxygen::serio::Writer;
using oxygen::serio::limits::SequenceSizeType;
using serialization_test::AlignedRecord;
using serialization_test::PackedRecord;

enum class Mode : uint16_t {
  kOff = 0,
  kOn = 7,
};

static_assert(oxygen::serio::RecordLayout<PackedRecord>);
static_assert(oxygen::serio::RecordLayout<AlignedRecord>);
static_assert(oxygen::serio::BulkElement<PackedRecord>);
static_assert(oxygen::serio::BulkElement<float>);
static_assert(!oxygen::serio::BulkElement<bool>);
static_assert(!oxygen::serio::BulkElement<std::string>);

//! Converts implicitly to an integer, and so to `bool`, but has no `Store`.
struct ImplicitCount {
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  operator uint32_t() const { return 1; }
};

template <class T>
concept Storable = requires(
  oxygen::serio::AnyWriter& writer, const T& value) { Store(writer, value); };

// A type without its own `Store` is refused, never written as a `bool`.
static_assert(Storable<bool>);
static_assert(!Storable<ImplicitCount>);

//! Writes values to a memory stream, then reads them back from its start.
class SerializationTest : public ::testing::Test {
protected:
  template <typename T> auto Write(const T& value) -> void
  {
    ASSERT_TRUE(writer_.Write(value));
  }

  template <typename T> auto ReadBack() -> oxygen::Result<T>
  {
    EXPECT_TRUE(stream_.Seek(ByteOffset { 0 }));
    return reader_.Read<T>();
  }

  auto Stream() -> MemoryStream& { return stream_; }
  auto GetReader() -> Reader<MemoryStream>& { return reader_; }

private:
  MemoryStream stream_;
  Writer<MemoryStream> writer_ { stream_ };
  Reader<MemoryStream> reader_ { stream_ };
};

//! A packed record is its bytes, written and read as one copy.
NOLINT_TEST_F(SerializationTest, PackedRecord_IsItsBytes)
{
  const auto record = PackedRecord {
    .kind = 3,
    .count = 42,
    .scale = 1.5F,
    .position = { 1.0F, -2.0F, 0.25F },
  };

  Write(record);

  ASSERT_EQ(Stream().Data().size(), sizeof(PackedRecord));
  const auto read = ReadBack<PackedRecord>();
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(read->kind, 3U);
  EXPECT_EQ(read->count, 42U);
  EXPECT_FLOAT_EQ(read->scale, 1.5F);
  EXPECT_FLOAT_EQ(read->position[1], -2.0F);
}

//! A record is aligned like any value: to its natural alignment.
NOLINT_TEST_F(SerializationTest, AlignedRecord_AlignsLikeAValue)
{
  Write(uint8_t { 1 });
  Write(AlignedRecord { .id = 7, .flags = 9, .size = 11 });

  EXPECT_EQ(
    Stream().Data().size(), alignof(AlignedRecord) + sizeof(AlignedRecord));
  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));
  ASSERT_TRUE(GetReader().Read<uint8_t>());
  const auto read = GetReader().Read<AlignedRecord>();
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(read->size, 11U);
}

//! A vector of records is its count, then the records back to back.
NOLINT_TEST_F(SerializationTest, RecordVector_IsCountThenRecords)
{
  const auto records = std::vector<PackedRecord> {
    { .kind = 1, .count = 2, .scale = 3.0F, .position = {} },
    { .kind = 4, .count = 5, .scale = 6.0F, .position = {} },
  };

  Write(records);

  EXPECT_EQ(Stream().Data().size(),
    sizeof(SequenceSizeType) + (2 * sizeof(PackedRecord)));
  const auto read = ReadBack<std::vector<PackedRecord>>();
  ASSERT_HAS_VALUE(read);
  ASSERT_EQ(read->size(), 2U);
  EXPECT_EQ((*read)[1].count, 5U);
}

//! Arithmetic vectors, arrays and spans share one encoding.
NOLINT_TEST_F(SerializationTest, ArithmeticSequences_ShareOneEncoding)
{
  const auto values = std::array<uint16_t, 3> { 10, 20, 30 };

  Write(values);
  Write(std::span<const uint16_t> { values });

  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));
  const auto as_array = GetReader().Read<std::array<uint16_t, 3>>();
  const auto as_vector = GetReader().Read<std::vector<uint16_t>>();
  ASSERT_HAS_VALUE(as_array);
  ASSERT_HAS_VALUE(as_vector);
  EXPECT_EQ(*as_array, values);
  EXPECT_EQ(*as_vector, std::vector<uint16_t>(values.begin(), values.end()));
}

//! An array read refuses a stored length other than its own.
NOLINT_TEST_F(SerializationTest, Array_RefusesAnotherLength)
{
  Write(std::vector<uint32_t> { 1, 2 });

  const auto read = ReadBack<std::array<uint32_t, 3>>();

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), std::errc::invalid_argument);
}

//! Elements that are not bulk-copied round-trip one by one.
NOLINT_TEST_F(SerializationTest, StringAndBoolVectors_RoundTrip)
{
  const auto names = std::vector<std::string> { "a", "", "long name" };
  const auto flags = std::vector<bool> { true, false, true };

  Write(names);
  Write(flags);

  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));
  const auto read_names = GetReader().Read<std::vector<std::string>>();
  const auto read_flags = GetReader().Read<std::vector<bool>>();
  ASSERT_HAS_VALUE(read_names);
  ASSERT_HAS_VALUE(read_flags);
  EXPECT_EQ(*read_names, names);
  EXPECT_EQ(*read_flags, flags);
}

//! A bool is one byte, 0 or 1; another byte is refused.
NOLINT_TEST_F(SerializationTest, Bool_IsOneByteAndRefusesOthers)
{
  Write(true);
  Write(uint8_t { 2 });

  ASSERT_EQ(Stream().Data().size(), 2U);
  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));
  const auto valid = GetReader().Read<bool>();
  const auto invalid = GetReader().Read<bool>();
  ASSERT_HAS_VALUE(valid);
  EXPECT_TRUE(*valid);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error(), std::errc::invalid_argument);
}

//! An optional is a presence flag, then its value when it has one.
NOLINT_TEST_F(SerializationTest, Optional_RoundTrips)
{
  Write(std::optional<uint32_t> { 5 });
  Write(std::optional<uint32_t> {});

  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));
  const auto present = GetReader().Read<std::optional<uint32_t>>();
  const auto absent = GetReader().Read<std::optional<uint32_t>>();
  ASSERT_HAS_VALUE(present);
  ASSERT_HAS_VALUE(absent);
  EXPECT_EQ(*present, std::optional<uint32_t> { 5 });
  EXPECT_FALSE(absent->has_value());
}

//! An enum is its underlying value.
NOLINT_TEST_F(SerializationTest, Enum_IsItsUnderlyingValue)
{
  Write(Mode::kOn);

  ASSERT_EQ(Stream().Data().size(), sizeof(uint16_t));
  const auto read = ReadBack<Mode>();
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(*read, Mode::kOn);
}

//! A length prefix that claims more elements than the bytes that remain is
//! refused before anything is allocated.
NOLINT_TEST_F(SerializationTest, LengthPrefix_IsBoundedByRemainingBytes)
{
  Write(SequenceSizeType { 1000 });
  Write(uint32_t { 1 });

  const auto read = ReadBack<std::vector<uint32_t>>();

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kTooLarge);
}

//! A string length that passes the bytes that remain is refused.
NOLINT_TEST_F(SerializationTest, StringLength_IsBoundedByRemainingBytes)
{
  Write(SequenceSizeType { 64 });

  const auto read = ReadBack<std::string>();

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kTooLarge);
}

//! A blob larger than the bytes that remain is refused before allocating.
NOLINT_TEST_F(SerializationTest, ReadBlob_IsBoundedByRemainingBytes)
{
  Write(uint64_t { 0 });
  ASSERT_TRUE(Stream().Seek(ByteOffset { 0 }));

  const auto too_large = GetReader().ReadBlob(9);
  const auto exact = GetReader().ReadBlob(8);

  ASSERT_FALSE(too_large);
  EXPECT_EQ(too_large.error(), Error::kTooLarge);
  ASSERT_HAS_VALUE(exact);
  EXPECT_EQ(exact->size(), 8U);
}

//! Remaining counts the bytes after the cursor.
NOLINT_TEST_F(SerializationTest, Remaining_CountsBytesAfterCursor)
{
  Write(uint64_t { 0 });
  ASSERT_TRUE(Stream().Seek(ByteOffset { 3 }));

  EXPECT_EQ(GetReader().Remaining().get(), 5U);
}

} // namespace
