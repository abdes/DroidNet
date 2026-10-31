//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Serio/CountingSink.h

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Serio/CountingSink.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteOffset;
using oxygen::serio::CountingSink;
using oxygen::serio::Error;
using oxygen::serio::MemoryStream;
using oxygen::serio::Writer;

static_assert(oxygen::serio::PatchableOutputStream<CountingSink>);

//! Writes the same mixed values through a writer to `stream`.
template <class S> auto WriteSample(S& stream) -> void
{
  auto writer = Writer<S> { stream };
  ASSERT_TRUE(writer.Write(uint8_t { 1 }));
  ASSERT_TRUE(writer.Write(std::string { "measured" }));
  ASSERT_TRUE(writer.Write(std::vector<double> { 1.0, 2.0 }));
  ASSERT_TRUE(writer.Write(true));
}

//! A sink measures exactly the bytes a real stream receives, padding included.
NOLINT_TEST(CountingSinkTest, MeasuresWhatAStreamReceives)
{
  auto sink = CountingSink {};
  auto stream = MemoryStream {};

  WriteSample(sink);
  WriteSample(stream);

  EXPECT_EQ(sink.Count().get(), stream.Data().size());
  EXPECT_EQ(sink.Position().get(), stream.Position().get());
}

//! A patch within the bytes counted succeeds; past them it fails.
NOLINT_TEST(CountingSinkTest, PatchAt_IsBoundedByCount)
{
  auto sink = CountingSink {};
  constexpr auto kBytes = std::array<std::byte, 4> {};
  ASSERT_TRUE(sink.Write(kBytes));

  const auto inside = sink.PatchAt(ByteOffset { 0 }, kBytes);
  const auto past
    = sink.PatchAt(ByteOffset { 1 }, std::span<const std::byte> { kBytes });

  EXPECT_TRUE(inside);
  ASSERT_FALSE(past);
  EXPECT_EQ(past.error(), Error::kOutOfBounds);
  EXPECT_EQ(sink.Count().get(), 4U);
}

} // namespace
