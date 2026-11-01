//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/ByteBuffer.h

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::AsUnsigned;
using oxygen::serio::ByteBuffer;
using oxygen::serio::ByteCount;
using oxygen::serio::kIoAlignment;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;

auto AddressOf(const ByteBuffer& buffer) -> uintptr_t
{
  return reinterpret_cast<uintptr_t>(buffer.Bytes().data()); // NOLINT
}

//! A buffer has the size asked for and the IO alignment by default.
NOLINT_TEST(ByteBufferTest, AllocatesAlignedBytes)
{
  const auto buffer = ByteBuffer::Allocate(ByteCount { 10'000 });

  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->Size().get(), 10'000U);
  EXPECT_EQ(buffer->Bytes().size(), 10'000U);
  EXPECT_EQ(AddressOf(*buffer) % kIoAlignment, 0U);
}

//! A requested alignment is honoured; one that is not a power of two is
//! refused.
NOLINT_TEST(ByteBufferTest, HonoursOrRefusesAlignment)
{
  const auto aligned = ByteBuffer::Allocate(ByteCount { 3 }, 64);
  const auto refused = ByteBuffer::Allocate(ByteCount { 3 }, 48);

  ASSERT_TRUE(aligned);
  EXPECT_EQ(AddressOf(*aligned) % 64, 0U);
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error(), std::errc::invalid_argument);
}

//! A zero-size buffer is empty and owns nothing.
NOLINT_TEST(ByteBufferTest, ZeroSizeIsEmpty)
{
  const auto buffer = ByteBuffer::Allocate(ByteCount { 0 });

  ASSERT_TRUE(buffer);
  EXPECT_TRUE(buffer->Empty());
  EXPECT_EQ(buffer->Bytes().data(), nullptr);
}

//! Truncate shrinks the size and keeps the storage; it never grows.
NOLINT_TEST(ByteBufferTest, TruncateOnlyShrinks)
{
  auto buffer = ByteBuffer::Allocate(ByteCount { 100 });
  ASSERT_TRUE(buffer);
  const auto* const data = buffer->Bytes().data();

  buffer->Truncate(ByteCount { 40 });
  buffer->Truncate(ByteCount { 90 });

  EXPECT_EQ(buffer->Size().get(), 40U);
  EXPECT_EQ(buffer->Bytes().data(), data);
}

//! Moving hands the storage over and leaves the source empty.
NOLINT_TEST(ByteBufferTest, MoveTransfersStorage)
{
  auto source = ByteBuffer::Allocate(ByteCount { 32 });
  ASSERT_TRUE(source);
  const auto* const data = source->Bytes().data();

  const auto target = std::move(source).value();

  EXPECT_EQ(target.Bytes().data(), data);
  EXPECT_EQ(target.Size().get(), 32U);
}

//! A copy holds the same bytes in its own storage.
NOLINT_TEST(ByteBufferTest, CopyOfCopiesTheBytes)
{
  const auto bytes = PatternBytes(300);

  const auto copy = ByteBuffer::CopyOf(bytes);

  ASSERT_TRUE(copy);
  EXPECT_THAT(copy->Bytes(), BytesEqual(bytes));
  EXPECT_NE(copy->Bytes().data(), bytes.data());
  EXPECT_EQ(AddressOf(*copy) % kIoAlignment, 0U);
}

//! A copy honours the alignment asked for.
NOLINT_TEST(ByteBufferTest, CopyOfHonoursAlignment)
{
  const auto bytes = PatternBytes(33);

  const auto copy = ByteBuffer::CopyOf(bytes, 128);

  ASSERT_TRUE(copy);
  EXPECT_EQ(AddressOf(*copy) % 128, 0U);
  EXPECT_THAT(copy->Bytes(), BytesEqual(bytes));
}

//! Copying nothing gives an empty buffer.
NOLINT_TEST(ByteBufferTest, CopyOfEmptyInputIsEmpty)
{
  const auto copy = ByteBuffer::CopyOf({});

  ASSERT_TRUE(copy);
  EXPECT_TRUE(copy->Empty());
}

//! An alignment that is not a power of two is refused, as by Allocate.
NOLINT_TEST(ByteBufferTest, CopyOfRefusesBadAlignment)
{
  const auto bytes = PatternBytes(8);

  const auto copy = ByteBuffer::CopyOf(bytes, 48);

  ASSERT_FALSE(copy);
  EXPECT_EQ(copy.error(), std::errc::invalid_argument);
}

//! The unsigned view of a buffer is its bytes: same address and size.
NOLINT_TEST(ByteBufferTest, UnsignedViewsTheSameBytes)
{
  const auto buffer = ByteBuffer::CopyOf(PatternBytes(40));
  ASSERT_TRUE(buffer);

  const auto view = buffer->Unsigned();

  ASSERT_EQ(view.size(), buffer->Bytes().size());
  EXPECT_EQ(static_cast<const void*>(view.data()),
    static_cast<const void*>(buffer->Bytes().data()));
  EXPECT_EQ(view.front(), static_cast<uint8_t>(buffer->Bytes().front()));
  EXPECT_EQ(view.back(), static_cast<uint8_t>(buffer->Bytes().back()));
}

//! AsUnsigned views read-only and writable bytes in place.
NOLINT_TEST(ByteBufferTest, AsUnsignedKeepsAddressAndSize)
{
  auto bytes = PatternBytes(16);

  const auto readable = AsUnsigned(std::span<const std::byte>(bytes));
  const auto writable = AsUnsigned(std::span<std::byte>(bytes));

  EXPECT_EQ(readable.size(), bytes.size());
  EXPECT_EQ(writable.size(), bytes.size());
  EXPECT_EQ(static_cast<const void*>(readable.data()),
    static_cast<const void*>(bytes.data()));
  EXPECT_EQ(static_cast<const void*>(writable.data()),
    static_cast<const void*>(bytes.data()));
  writable[0] = 0xAB;
  EXPECT_EQ(bytes[0], std::byte { 0xAB });
}

} // namespace
