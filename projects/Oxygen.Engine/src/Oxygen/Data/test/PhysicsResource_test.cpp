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

#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::data::PhysicsResource;

namespace {

constexpr auto kBytes = std::array<uint8_t, 5> { 1, 3, 5, 7, 9 };

//! A physics resource keeps the buffer its loader read the payload into: no
//! copy.
NOLINT_TEST(PhysicsResourceTest, OwnsTheBufferItIsGiven)
{
  auto buffer
    = oxygen::serio::ByteBuffer::CopyOf(std::as_bytes(std::span { kBytes }))
        .value();
  const auto* const storage = buffer.Unsigned().data();
  const auto desc = oxygen::data::pak::physics::PhysicsResourceDesc {
    .size_bytes = static_cast<uint32_t>(kBytes.size()),
  };

  const PhysicsResource resource { desc, std::move(buffer) };

  EXPECT_EQ(resource.GetData().data(), storage);
  EXPECT_EQ(resource.GetDataSize(), kBytes.size());
  EXPECT_TRUE(std::ranges::equal(resource.GetData(), kBytes));
}

//! An empty payload yields an empty view.
NOLINT_TEST(PhysicsResourceTest, EmptyPayloadIsEmpty)
{
  const PhysicsResource resource {
    oxygen::data::pak::physics::PhysicsResourceDesc {},
    oxygen::serio::ByteBuffer {},
  };

  EXPECT_EQ(resource.GetDataSize(), 0U);
  EXPECT_TRUE(resource.GetData().empty());
}

//! A moved resource keeps viewing the same bytes.
NOLINT_TEST(PhysicsResourceTest, MoveKeepsTheSameBytes)
{
  auto buffer
    = oxygen::serio::ByteBuffer::CopyOf(std::as_bytes(std::span { kBytes }))
        .value();
  const auto* const storage = buffer.Unsigned().data();
  PhysicsResource original {
    oxygen::data::pak::physics::PhysicsResourceDesc {},
    std::move(buffer),
  };

  const PhysicsResource moved { std::move(original) };

  EXPECT_EQ(moved.GetData().data(), storage);
}

} // namespace
