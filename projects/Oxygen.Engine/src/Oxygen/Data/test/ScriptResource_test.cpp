//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::data::ScriptResource;

namespace {

constexpr auto kBytes = std::array<uint8_t, 6> { 9, 8, 7, 6, 5, 4 };

//! A script resource keeps the buffer its loader read the payload into: no
//! copy.
NOLINT_TEST(ScriptResourceTest, OwnsTheBufferItIsGiven)
{
  auto buffer
    = oxygen::serio::ByteBuffer::CopyOf(std::as_bytes(std::span { kBytes }))
        .value();
  const auto* const storage = buffer.Unsigned().data();
  const auto desc = oxygen::data::pak::scripting::ScriptResourceDesc {
    .size_bytes = static_cast<uint32_t>(kBytes.size()),
  };

  const ScriptResource resource { desc, std::move(buffer) };

  EXPECT_EQ(resource.GetData().data(), storage);
  EXPECT_EQ(resource.GetDataSize(), kBytes.size());
  EXPECT_TRUE(std::ranges::equal(resource.GetData(), kBytes));
}

//! An empty payload yields an empty view.
NOLINT_TEST(ScriptResourceTest, EmptyPayloadIsEmpty)
{
  const ScriptResource resource {
    oxygen::data::pak::scripting::ScriptResourceDesc {},
    oxygen::serio::ByteBuffer {},
  };

  EXPECT_EQ(resource.GetDataSize(), 0U);
  EXPECT_TRUE(resource.GetData().empty());
}

//! A moved resource keeps viewing the same bytes.
NOLINT_TEST(ScriptResourceTest, MoveKeepsTheSameBytes)
{
  auto buffer
    = oxygen::serio::ByteBuffer::CopyOf(std::as_bytes(std::span { kBytes }))
        .value();
  const auto* const storage = buffer.Unsigned().data();
  const auto desc = oxygen::data::pak::scripting::ScriptResourceDesc {
    .size_bytes = static_cast<uint32_t>(kBytes.size()),
  };
  ScriptResource original { desc, std::move(buffer) };

  const ScriptResource moved { std::move(original) };

  EXPECT_EQ(moved.GetData().data(), storage);
}

} // namespace
