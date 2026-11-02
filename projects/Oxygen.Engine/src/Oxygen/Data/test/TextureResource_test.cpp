//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::cooker::testing::TextureBuilder;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
using oxygen::data::TextureResource;
using oxygen::serio::ByteBuffer;

//! A 128x64 RGBA8 2D texture with five mips, packed for D3D12.
[[nodiscard]] auto Texture() -> TextureBuilder
{
  return TextureBuilder {}.Size({ .width = 128U, .height = 64U }).Mips(5U);
}

NOLINT_TEST(TextureResourceTest, AccessorsReturnTheRowAndPayloadValues)
{
  const auto texture = Texture();
  const auto payload = texture.Payload();
  auto desc = texture.Record(payload);
  desc.data_offset = 4096U;

  const auto tex
    = TextureResource { desc, ByteBuffer::CopyOf(payload).value() };

  const auto header = TextureBuilder::HeaderAt(payload);
  EXPECT_EQ(tex.GetDataOffset(), 4096U);
  EXPECT_EQ(tex.GetPayload().size(), payload.size());
  EXPECT_EQ(tex.GetDataSize(), payload.size() - header.data_offset_bytes);
  EXPECT_EQ(tex.GetTextureType(), TextureType::kTexture2D);
  EXPECT_EQ(tex.GetFormat(), Format::kRGBA8UNorm);
  EXPECT_EQ(tex.GetWidth(), 128U);
  EXPECT_EQ(tex.GetHeight(), 64U);
  EXPECT_EQ(tex.GetDepth(), 1U);
  EXPECT_EQ(tex.GetArrayLayers(), 1U);
  EXPECT_EQ(tex.GetMipCount(), 5U);
  EXPECT_EQ(tex.GetDataAlignment(), 256U);
  EXPECT_EQ(tex.GetPayloadHeader().subresource_count, 5U);
  ASSERT_EQ(tex.GetSubresourceLayouts().size(), 5U);
  EXPECT_EQ(tex.GetSubresourceLayouts()[2].size_bytes,
    TextureBuilder::SubresourceAt(payload, 2U).size_bytes);
}

NOLINT_TEST(TextureResourceTest, MoveTransfersThePayload)
{
  const auto texture = Texture();
  const auto payload = texture.Payload();
  auto original = TextureResource { texture.Record(payload),
    ByteBuffer::CopyOf(payload).value() };
  const auto data_size = original.GetDataSize();

  const auto moved = std::move(original);

  EXPECT_EQ(moved.GetDataSize(), data_size);
  EXPECT_EQ(original.GetDataSize(), 0U); // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(original.GetPayload().size(), 0U);
  EXPECT_TRUE(original.GetSubresourceLayouts().empty());
}

//! A texture keeps the buffer its loader read the payload into: no copy.
NOLINT_TEST(TextureResourceTest, OwnsTheBufferItIsGiven)
{
  const auto texture = Texture();
  const auto payload = texture.Payload();
  auto buffer = ByteBuffer::CopyOf(payload).value();
  const auto* const storage = buffer.Unsigned().data();

  const auto tex
    = TextureResource { texture.Record(payload), std::move(buffer) };

  EXPECT_EQ(tex.GetPayload().data(), storage);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(tex.GetPayload()), payload));
}

//! The reader checks the row's size against the payload it is given, as the
//! loader checks the bytes it reads.
NOLINT_TEST(TextureResourceTest, RefusesAPayloadOfAnotherSize)
{
  const auto texture = Texture();
  const auto payload = texture.Payload();
  auto desc = texture.Record(payload);
  desc.size_bytes += 1U;

  auto refused = std::optional<CookedDefect> {};
  try {
    static_cast<void>(
      TextureResource { desc, ByteBuffer::CopyOf(payload).value() });
  } catch (const CookedOpenError& error) {
    refused = error.Defect();
  }
  EXPECT_EQ(refused, CookedDefect::kSizeMismatch);
}

} // namespace
