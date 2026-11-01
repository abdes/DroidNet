//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {

//! The typed input stream conformance suite.
/*!
 Every input stream runs the same cases, whatever it reads from. A stream
 joins with a factory type and one line:

 ```cpp
 struct MyStreamFactory {
   using Stream = MyStream;
   auto Make(std::span<const std::byte> bytes) -> Stream;
 };
 using Factories = ::testing::Types<MyStreamFactory>;
 INSTANTIATE_TYPED_TEST_SUITE_P(MyModule, InputStreamConformance, Factories);
 ```

 `Make` returns a stream positioned at the start of `bytes`. The fixture owns
 one factory per test, which owns whatever the stream reads.

 @tparam Factory The stream factory.
*/
template <class Factory> class InputStreamConformance : public ::testing::Test {
protected:
  //! Larger than any stream buffer by default, so reads cross buffer refills
  //! and the largest reads bypass the buffer.
  static constexpr size_t kSize = (200ULL * 1024) + 13;

  auto MakeStream(const std::span<const std::byte> bytes) -> Factory::Stream
  {
    return factory_.Make(bytes);
  }

private:
  Factory factory_ {};
};

TYPED_TEST_SUITE_P(InputStreamConformance);

//! Size is the content size, and a new stream is at its start.
TYPED_TEST_P(InputStreamConformance, StartsAtZeroWithContentSize) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);

  EXPECT_EQ(stream.Size().get(), bytes.size());
  EXPECT_EQ(stream.Position().get(), 0U);
}

//! ReadSome returns bytes from the cursor and advances it by their count.
TYPED_TEST_P(InputStreamConformance, ReadSomeAdvancesCursor) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  auto out = std::vector<std::byte>(100);

  const auto read = stream.ReadSome(out);

  ASSERT_TRUE(read);
  ASSERT_GT(read->get(), 0U);
  EXPECT_EQ(stream.Position().get(), read->get());
  EXPECT_THAT(std::span(out).first(read->get()),
    BytesEqual(std::span(bytes).first(read->get())));
}

//! Small reads in sequence reassemble the content, then return nothing.
TYPED_TEST_P(InputStreamConformance, SequentialReadsReassembleContent) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  auto collected = std::vector<std::byte> {};
  auto chunk = std::vector<std::byte>(4093);

  for (;;) {
    const auto read = stream.ReadSome(chunk);
    ASSERT_TRUE(read);
    if (read->get() == 0) {
      break;
    }
    collected.insert(collected.end(), chunk.begin(),
      chunk.begin() + static_cast<std::ptrdiff_t>(read->get()));
  }

  EXPECT_THAT(collected, BytesEqual(bytes));
  EXPECT_EQ(stream.Position().get(), bytes.size());
}

//! One exact read of the whole content returns it.
TYPED_TEST_P(InputStreamConformance, LargeExactReadReturnsContent) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  auto out = std::vector<std::byte>(bytes.size());

  ASSERT_TRUE(ReadExactly(stream, out));

  EXPECT_THAT(out, BytesEqual(bytes));
}

//! An exact read past the end fails with kEndOfData, consuming what remains.
TYPED_TEST_P(InputStreamConformance, ExactReadAcrossEndIsEndOfData) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  ASSERT_TRUE(stream.Seek(ByteOffset { bytes.size() - 10 }));
  auto out = std::vector<std::byte>(20);

  const auto read = ReadExactly(stream, out);

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kEndOfData);
  EXPECT_EQ(stream.Position().get(), bytes.size());
}

//! A zero-length read succeeds and leaves the cursor in place.
TYPED_TEST_P(InputStreamConformance, ZeroLengthReadSucceeds) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  ASSERT_TRUE(stream.Seek(ByteOffset { 5 }));

  const auto read = stream.ReadSome({});

  ASSERT_TRUE(read);
  EXPECT_EQ(read->get(), 0U);
  EXPECT_EQ(stream.Position().get(), 5U);
}

//! Seeking back after a read far ahead reads the earlier bytes again.
TYPED_TEST_P(InputStreamConformance, SeekBackwardRereads) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  auto ahead = std::vector<std::byte>(300);
  auto back = std::vector<std::byte>(300);
  ASSERT_TRUE(stream.Seek(ByteOffset { 150'000 }));
  ASSERT_TRUE(ReadExactly(stream, ahead));

  ASSERT_TRUE(stream.Seek(ByteOffset { 10 }));
  ASSERT_TRUE(ReadExactly(stream, back));

  EXPECT_THAT(ahead, BytesEqual(std::span(bytes).subspan(150'000, 300)));
  EXPECT_THAT(back, BytesEqual(std::span(bytes).subspan(10, 300)));
}

//! Seeking to the end succeeds; past it fails with kOutOfBounds and keeps the
//! cursor.
TYPED_TEST_P(InputStreamConformance, SeekPastEndIsOutOfBounds) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);
  ASSERT_TRUE(stream.Seek(ByteOffset { 42 }));

  const auto past = stream.Seek(ByteOffset { bytes.size() + 1 });
  const auto position_after_failure = stream.Position();
  const auto end = stream.Seek(ByteOffset { bytes.size() });
  auto out = std::vector<std::byte>(4);
  const auto read_at_end = stream.ReadSome(out);

  ASSERT_FALSE(past);
  EXPECT_EQ(past.error(), Error::kOutOfBounds);
  EXPECT_EQ(position_after_failure.get(), 42U);
  EXPECT_TRUE(end);
  ASSERT_TRUE(read_at_end);
  EXPECT_EQ(read_at_end->get(), 0U);
}

//! Skip moves the cursor forward; past the end it fails with kOutOfBounds.
TYPED_TEST_P(InputStreamConformance, SkipIsBounded) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  auto stream = this->MakeStream(bytes);

  const auto skipped = Skip(stream, ByteCount { 1000 });
  const auto past = Skip(stream, ByteCount { bytes.size() });

  EXPECT_TRUE(skipped);
  EXPECT_EQ(stream.Position().get(), 1000U);
  ASSERT_FALSE(past);
  EXPECT_EQ(past.error(), Error::kOutOfBounds);
  EXPECT_EQ(stream.Position().get(), 1000U);
}

REGISTER_TYPED_TEST_SUITE_P(InputStreamConformance, StartsAtZeroWithContentSize,
  ReadSomeAdvancesCursor, SequentialReadsReassembleContent,
  LargeExactReadReturnsContent, ExactReadAcrossEndIsEndOfData,
  ZeroLengthReadSucceeds, SeekBackwardRereads, SeekPastEndIsOutOfBounds,
  SkipIsBounded);

//! The typed output stream conformance suite.
/*!
 Every output stream runs the same cases, whatever it writes to. A stream
 joins with a factory type and one line:

 ```cpp
 struct MyStreamFactory {
   using Stream = MyStream;
   auto Make() -> Stream;
   auto Contents(Stream& stream) -> std::vector<std::byte>;
 };
 using Factories = ::testing::Types<MyStreamFactory>;
 INSTANTIATE_TYPED_TEST_SUITE_P(MyModule, OutputStreamConformance, Factories);
 ```

 `Make` returns an empty stream; `Contents` returns what has reached the
 stream's destination, after the case flushed it. A stream that satisfies
 `PatchableOutputStream` also joins `PatchableOutputStreamConformance`.

 @tparam Factory The stream factory.
*/
template <class Factory>
class OutputStreamConformance : public ::testing::Test {
protected:
  using Stream = Factory::Stream;

  //! Larger than any stream buffer by default, so some bytes reach the
  //! destination before the final flush and others are still buffered.
  static constexpr size_t kLargeSize = (2560ULL * 1024) + 7;

  auto MakeStream() -> Stream { return factory_.Make(); }

  auto Contents(Stream& stream) -> std::vector<std::byte>
  {
    return factory_.Contents(stream);
  }

private:
  Factory factory_ {};
};

TYPED_TEST_SUITE_P(OutputStreamConformance);

//! Writes of every size reach the destination in order, and the cursor
//! counts them.
TYPED_TEST_P(OutputStreamConformance, WritesArriveInOrder) // NOLINT
{
  const auto large = PatternBytes(TestFixture::kLargeSize);
  const auto small = PatternBytes(17, PatternSeed::kSecond);
  auto stream = this->MakeStream();

  ASSERT_TRUE(stream.Write(small));
  ASSERT_TRUE(stream.Write(large));
  ASSERT_TRUE(stream.Write(small));
  ASSERT_TRUE(stream.Flush());

  auto expected = small;
  expected.insert(expected.end(), large.begin(), large.end());
  expected.insert(expected.end(), small.begin(), small.end());
  EXPECT_EQ(stream.Position().get(), expected.size());
  EXPECT_THAT(this->Contents(stream), BytesEqual(expected));
}

//! A zero-length write and a repeated flush change nothing.
TYPED_TEST_P(OutputStreamConformance, EmptyWriteAndRepeatedFlush) // NOLINT
{
  const auto bytes = PatternBytes(64);
  auto stream = this->MakeStream();

  ASSERT_TRUE(stream.Write(bytes));
  ASSERT_TRUE(stream.Write({}));
  ASSERT_TRUE(stream.Flush());
  ASSERT_TRUE(stream.Flush());

  EXPECT_EQ(stream.Position().get(), bytes.size());
  EXPECT_THAT(this->Contents(stream), BytesEqual(bytes));
}

REGISTER_TYPED_TEST_SUITE_P(
  OutputStreamConformance, WritesArriveInOrder, EmptyWriteAndRepeatedFlush);

//! The cases of output streams that patch written bytes:
//! `OutputStreamConformance`'s fixture, for factories whose streams satisfy
//! `PatchableOutputStream`.
template <class Factory>
class PatchableOutputStreamConformance
  : public OutputStreamConformance<Factory> {
  static_assert(PatchableOutputStream<typename Factory::Stream>);
};

TYPED_TEST_SUITE_P(PatchableOutputStreamConformance);

//! PatchAt overwrites bytes already at the destination and bytes still
//! buffered, and leaves the cursor where it was.
TYPED_TEST_P(
  PatchableOutputStreamConformance, PatchAtOverwritesWrittenBytes) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kLargeSize);
  const auto head = PatternBytes(16, PatternSeed::kSecond);
  const auto tail = PatternBytes(8, PatternSeed::kSecond);
  auto stream = this->MakeStream();
  ASSERT_TRUE(stream.Write(bytes));

  ASSERT_TRUE(stream.PatchAt(ByteOffset { 10 }, head));
  ASSERT_TRUE(stream.PatchAt(ByteOffset { bytes.size() - tail.size() }, tail));
  ASSERT_TRUE(stream.Flush());

  auto expected = bytes;
  std::ranges::copy(head, expected.begin() + 10);
  std::ranges::copy(
    tail, expected.end() - static_cast<std::ptrdiff_t>(tail.size()));
  EXPECT_EQ(stream.Position().get(), bytes.size());
  EXPECT_THAT(this->Contents(stream), BytesEqual(expected));
}

//! PatchAt past the cursor fails with kOutOfBounds and changes nothing.
TYPED_TEST_P(
  PatchableOutputStreamConformance, PatchAtPastCursorIsOutOfBounds) // NOLINT
{
  const auto bytes = PatternBytes(32);
  const auto patch = PatternBytes(8, PatternSeed::kSecond);
  auto stream = this->MakeStream();
  ASSERT_TRUE(stream.Write(bytes));

  const auto across = stream.PatchAt(ByteOffset { 28 }, patch);
  const auto beyond = stream.PatchAt(ByteOffset { 40 }, patch);
  ASSERT_TRUE(stream.Flush());

  ASSERT_FALSE(across);
  EXPECT_EQ(across.error(), Error::kOutOfBounds);
  ASSERT_FALSE(beyond);
  EXPECT_EQ(beyond.error(), Error::kOutOfBounds);
  EXPECT_THAT(this->Contents(stream), BytesEqual(bytes));
}

REGISTER_TYPED_TEST_SUITE_P(PatchableOutputStreamConformance,
  PatchAtOverwritesWrittenBytes, PatchAtPastCursorIsOutOfBounds);

} // namespace oxygen::serio::testing
