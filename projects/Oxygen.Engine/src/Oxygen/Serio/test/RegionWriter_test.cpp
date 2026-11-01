//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/RegionWriter.h

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/RegionWriter.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::AnyWriter;
using oxygen::serio::ByteOffset;
using oxygen::serio::MemoryFile;
using oxygen::serio::RegionWriter;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;

using CountedDevice = FaultInjectingDevice<MemoryFile>;

static_assert(std::derived_from<RegionWriter<MemoryFile>, AnyWriter>);

//! A viewed device the writer writes; tests read what reached it.
class RegionWriterTest : public ::testing::Test {
protected:
  [[nodiscard]] auto Content() const -> std::span<const std::byte>
  {
    return device_.Inner().Bytes();
  }
  auto Device() -> CountedDevice& { return device_; }

private:
  CountedDevice device_ { MemoryFile {} };
};

//! Values written reach the device once the writer is flushed, not before.
NOLINT_TEST_F(RegionWriterTest, WrittenBytesReachTheDeviceAfterFlush)
{
  const auto bytes = PatternBytes(200);
  auto writer = RegionWriter<CountedDevice>(Device());

  ASSERT_HAS_VALUE(writer.WriteBlob(bytes));
  const auto before = Content().size();
  ASSERT_HAS_VALUE(writer.Flush());

  EXPECT_EQ(before, 0U);
  EXPECT_THAT(Content(), BytesEqual(bytes));
  EXPECT_EQ(writer.Position().value(), bytes.size());
}

//! Typed values written through the writer are all written.
NOLINT_TEST_F(RegionWriterTest, TypedValuesAreWritten)
{
  auto writer = RegionWriter<CountedDevice>(Device());

  ASSERT_HAS_VALUE(writer.Write(uint32_t { 0x01020304U }));
  ASSERT_HAS_VALUE(writer.Write(uint16_t { 0x0506U }));
  ASSERT_HAS_VALUE(writer.Flush());

  EXPECT_EQ(Content().size(), 6U);
}

//! The start offset is honoured: earlier bytes stay, and positions count from
//! the start.
NOLINT_TEST_F(RegionWriterTest, StartOffsetIsHonoured)
{
  const auto prefix = PatternBytes(64, PatternSeed::kSecond);
  const auto bytes = PatternBytes(40);
  ASSERT_HAS_VALUE(Device().WriteAt(ByteOffset { 0 }, prefix));
  auto writer = RegionWriter<CountedDevice>(Device(), ByteOffset { 64 });

  ASSERT_HAS_VALUE(writer.WriteBlob(bytes));
  ASSERT_HAS_VALUE(writer.Flush());

  EXPECT_EQ(writer.Position().value(), 40U);
  ASSERT_EQ(Content().size(), 104U);
  EXPECT_THAT(Content().first(64), BytesEqual(prefix));
  EXPECT_THAT(Content().subspan(64), BytesEqual(bytes));
}

//! A writer destroyed with buffered bytes flushes them to a viewed device.
NOLINT_TEST_F(RegionWriterTest, DestructionFlushesBufferedBytes)
{
  const auto bytes = PatternBytes(100);

  {
    auto writer = RegionWriter<CountedDevice>(Device());
    ASSERT_HAS_VALUE(writer.WriteBlob(bytes));
    EXPECT_TRUE(Content().empty());
  }

  EXPECT_THAT(Content(), BytesEqual(bytes));
}

//! The stream patches bytes already written, buffered or not, without moving
//! the writer's position.
NOLINT_TEST_F(RegionWriterTest, StreamPatchesWrittenBytes)
{
  const auto bytes = PatternBytes(32);
  const auto patch = PatternBytes(4, PatternSeed::kSecond);
  auto writer = RegionWriter<CountedDevice>(Device());
  ASSERT_HAS_VALUE(writer.WriteBlob(bytes));

  ASSERT_HAS_VALUE(writer.Stream().PatchAt(ByteOffset { 8 }, patch));
  ASSERT_HAS_VALUE(writer.Flush());

  EXPECT_EQ(writer.Position().value(), bytes.size());
  ASSERT_EQ(Content().size(), bytes.size());
  EXPECT_THAT(Content().first(8), BytesEqual(std::span(bytes).first(8)));
  EXPECT_THAT(Content().subspan(8, 4), BytesEqual(patch));
  EXPECT_THAT(Content().subspan(12), BytesEqual(std::span(bytes).subspan(12)));
}

//! An owning writer keeps its device: the bytes written are in Device().
NOLINT_TEST(RegionWriterOwningTest, DeviceHoldsTheWrittenBytes)
{
  const auto bytes = PatternBytes(300);
  auto writer = RegionWriter<MemoryFile>::Owning(MemoryFile {});
  ASSERT_HAS_VALUE(writer);

  ASSERT_HAS_VALUE((*writer)->WriteBlob(bytes));
  ASSERT_HAS_VALUE((*writer)->Flush());

  EXPECT_THAT((*writer)->Device().Bytes(), BytesEqual(bytes));
}

//! An owning writer starts at the offset asked for.
NOLINT_TEST(RegionWriterOwningTest, StartOffsetIsHonoured)
{
  const auto prefix = PatternBytes(16, PatternSeed::kSecond);
  const auto bytes = PatternBytes(16);
  auto file = MemoryFile {};
  ASSERT_HAS_VALUE(file.WriteAt(ByteOffset { 0 }, prefix));
  auto writer
    = RegionWriter<MemoryFile>::Owning(std::move(file), ByteOffset { 16 });
  ASSERT_HAS_VALUE(writer);

  ASSERT_HAS_VALUE((*writer)->WriteBlob(bytes));
  ASSERT_HAS_VALUE((*writer)->Flush());

  const auto content = (*writer)->Device().Bytes();
  ASSERT_EQ(content.size(), 32U);
  EXPECT_THAT(content.first(16), BytesEqual(prefix));
  EXPECT_THAT(content.subspan(16), BytesEqual(bytes));
}

} // namespace
