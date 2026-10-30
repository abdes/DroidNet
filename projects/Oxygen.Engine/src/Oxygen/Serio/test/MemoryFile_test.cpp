//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/MemoryFile.h, Serio/MemoryFile.cpp

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteBuffer;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::MemoryFile;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;

//! Device behavior shared with every device is in the conformance suite; these
//! cases cover what only a memory file adds: owning, adopting and handing on
//! its bytes.
class MemoryFileTest : public ::testing::Test {
protected:
  const std::vector<std::byte> bytes_ = PatternBytes(300);
};

//! An adopted buffer becomes the file's bytes without a copy.
NOLINT_TEST_F(MemoryFileTest, AdoptsBufferWithoutCopy)
{
  auto buffer = ByteBuffer::Allocate(ByteCount { bytes_.size() });
  ASSERT_TRUE(buffer);
  std::memcpy(buffer->Bytes().data(), bytes_.data(), bytes_.size());
  const auto* const data = buffer->Bytes().data();

  const auto file = MemoryFile(std::move(buffer).value());

  EXPECT_EQ(file.Bytes().data(), data);
  EXPECT_THAT(file.Bytes(), BytesEqual(bytes_));
}

//! Take hands on exactly the written bytes and leaves the file empty.
NOLINT_TEST_F(MemoryFileTest, TakeHandsOnWrittenBytes)
{
  auto file = MemoryFile {};
  ASSERT_TRUE(file.Reserve(ByteCount { 4096 }));
  ASSERT_TRUE(file.WriteAt(ByteOffset { 0 }, bytes_));
  const auto* const data = file.Bytes().data();

  const auto taken = file.Take();

  EXPECT_EQ(taken.Bytes().data(), data);
  EXPECT_THAT(taken.Bytes(), BytesEqual(bytes_));
  EXPECT_TRUE(file.Bytes().empty());
  EXPECT_EQ(file.Size()->get(), 0U);
}

//! Growing past the reserved storage keeps the bytes already written.
NOLINT_TEST_F(MemoryFileTest, GrowthKeepsContent)
{
  auto file = MemoryFile {};
  ASSERT_TRUE(file.WriteAt(ByteOffset { 0 }, std::span(bytes_).first(10)));

  ASSERT_TRUE(file.WriteAt(ByteOffset { 10 }, std::span(bytes_).subspan(10)));

  EXPECT_THAT(file.Bytes(), BytesEqual(bytes_));
}

//! A moved-from file is empty and usable.
NOLINT_TEST_F(MemoryFileTest, MovedFromIsEmpty)
{
  auto source = MemoryFile::FromBytes(bytes_);
  ASSERT_TRUE(source);

  const auto target = std::move(*source);

  EXPECT_THAT(target.Bytes(), BytesEqual(bytes_));
  EXPECT_TRUE(source->Bytes().empty()); // NOLINT(*-use-after-move)
  EXPECT_TRUE(source->WriteAt(ByteOffset { 0 }, bytes_));
}

} // namespace
