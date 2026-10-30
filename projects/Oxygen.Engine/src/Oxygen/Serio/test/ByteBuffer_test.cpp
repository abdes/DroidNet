//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/ByteBuffer.h

#include <cstdint>
#include <system_error>
#include <utility>

#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteBuffer;
using oxygen::serio::ByteCount;
using oxygen::serio::kIoAlignment;

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

} // namespace
