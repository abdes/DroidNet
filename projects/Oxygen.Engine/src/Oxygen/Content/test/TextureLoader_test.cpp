//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>

#include "Fixtures/LoaderTestFixtures.h"

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Testing/GTest.h>

using testing::NotNull;

using oxygen::content::loaders::LoadTextureResource;
using oxygen::cooker::testing::TextureBuilder;

namespace {

//! The texture loader reads a row from the descriptor stream and the payload
//! its resource context read; the rules it applies to them are covered by
//! Data's `TextureStructure` tests, through this loader.
class TextureLoaderTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  auto CreateLoaderContext() -> oxygen::content::LoaderContext
  {
    return MakeResourceContext<oxygen::data::pak::core::TextureResourceDesc>();
  }

  auto WriteDescriptorAndPayload(
    const oxygen::data::pak::core::TextureResourceDesc& desc,
    const std::span<const std::byte> payload) -> void
  {
    auto pack_desc = desc_writer_.ScopedAlignment(1);
    const auto desc_result = desc_writer_.Write(desc);
    if (!desc_result) {
      throw std::runtime_error(
        "failed to write descriptor: " + desc_result.error().message());
    }
    const auto payload_result = data_writer_.WriteBlob(payload);
    if (!payload_result) {
      throw std::runtime_error(
        "failed to write payload: " + payload_result.error().message());
    }
  }
};

NOLINT_TEST_F(TextureLoaderTest, LoadsTheTextureItsContextRead)
{
  const auto texture = TextureBuilder {}
                         .PixelFormat(oxygen::Format::kRGBA16Float)
                         .Size({ .width = 128U, .height = 64U })
                         .Mips(3U);
  const auto payload = texture.Payload();
  WriteDescriptorAndPayload(texture.Record(payload), payload);

  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetTextureType(), oxygen::TextureType::kTexture2D);
  EXPECT_EQ(asset->GetFormat(), oxygen::Format::kRGBA16Float);
  EXPECT_EQ(asset->GetWidth(), 128U);
  EXPECT_EQ(asset->GetHeight(), 64U);
  EXPECT_EQ(asset->GetMipCount(), 3U);
  EXPECT_EQ(asset->GetSubresourceLayouts().size(), 3U);
  const auto data = std::span(payload).subspan(
    TextureBuilder::HeaderAt(payload).data_offset_bytes);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(asset->GetData()), data));
}

} // namespace
