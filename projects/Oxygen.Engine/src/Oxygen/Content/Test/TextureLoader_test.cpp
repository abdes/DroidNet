//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Writer.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "Mocks/MockStream.h"
#include "Utils/PakUtils.h"

using testing::NotNull;

using oxygen::content::loaders::LoadTextureResource;
using oxygen::serio::Reader;

namespace {

//=== TextureLoader v4 Tests ===--------------------------------------------//

class TextureLoaderBasicTestTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  TextureLoaderBasicTestTest()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  auto CreateLoaderContext() -> oxygen::content::LoaderContext
  {
    if (!desc_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek data_stream");
    }

    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &desc_reader_,
      .data_readers = std::make_tuple(&data_reader_, &data_reader_),
      .work_offline = false,
    };
  }

  auto WriteDescriptorAndPayload(
    const oxygen::data::pak::TextureResourceDesc& desc,
    const std::vector<uint8_t>& payload) -> void
  {
    auto pack_desc = desc_writer_.ScopedAlignment(1);
    const auto desc_result = desc_writer_.Write(desc);
    if (!desc_result) {
      throw std::runtime_error(
        "failed to write descriptor: " + desc_result.error().message());
    }

    auto pack_data = data_writer_.ScopedAlignment(desc.alignment);

    const auto pos = data_writer_.Position();
    if (!pos) {
      throw std::runtime_error(
        "failed to query data writer position: " + pos.error().message());
    }
    if (pos.value() > desc.data_offset) {
      throw std::runtime_error("data writer already past data_offset");
    }
    if (pos.value() < desc.data_offset) {
      const auto pad = static_cast<size_t>(desc.data_offset - pos.value());
      std::vector<std::byte> zeros(pad, std::byte { 0 });
      const auto pad_result = data_writer_.WriteBlob(
        std::span<const std::byte>(zeros.data(), zeros.size()));
      if (!pad_result) {
        throw std::runtime_error(
          "failed to pad to data_offset: " + pad_result.error().message());
      }
    }

    const auto payload_result
      = data_writer_.WriteBlob(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
    if (!payload_result) {
      throw std::runtime_error(
        "failed to write payload: " + payload_result.error().message());
    }
  }

  MockStream desc_stream_;
  MockStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader<MockStream> desc_reader_;
  Reader<MockStream> data_reader_;
};

//! Test: valid v4 payload loads and preserves dimensions/format.
NOLINT_TEST_F(TextureLoaderBasicTestTest, LoadTexture_V4Payload_Succeeds)
{
  using oxygen::content::testing::MakeV4TexturePayload;
  using oxygen::data::pak::TextureResourceDesc;

  TextureResourceDesc desc {
    .data_offset = 256,
    .size_bytes = 0, // filled below
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2DArray),
    .compression_type = 0,
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<uint8_t>(oxygen::Format::kR8SInt),
    .alignment = 256,
    .content_hash = 0,
  };

  const auto payload_bytes = MakeV4TexturePayload(512U, std::byte { 0xAB });
  desc.size_bytes = static_cast<uint32_t>(payload_bytes.size());

  WriteDescriptorAndPayload(desc, payload_bytes);

  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetWidth(), 128u);
  EXPECT_EQ(asset->GetHeight(), 64u);
  EXPECT_EQ(asset->GetMipCount(), 1u);
  EXPECT_EQ(asset->GetArrayLayers(), 1u);
  EXPECT_EQ(asset->GetFormat(), oxygen::Format::kR8SInt);
  EXPECT_EQ(asset->GetData().size(),
    payload_bytes.size()
      - static_cast<size_t>(sizeof(oxygen::data::pak::TexturePayloadHeader))
      - sizeof(oxygen::data::pak::SubresourceLayout));
  EXPECT_THAT(asset->GetData(),
    ::testing::Each(static_cast<uint8_t>(std::byte { 0xAB })));
}

//! Test: invalid format enumerant maps to kUnknown but still loads.
NOLINT_TEST_F(TextureLoaderBasicTestTest, LoadTexture_InvalidFormat_IsUnknown)
{
  using oxygen::content::testing::MakeV4TexturePayload;
  using oxygen::data::pak::TextureResourceDesc;

  TextureResourceDesc desc {
    .data_offset = 256,
    .size_bytes = 0,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = 255,
    .alignment = 256,
    .content_hash = 0,
  };

  const auto payload_bytes = MakeV4TexturePayload(64U, std::byte { 0x11 });
  desc.size_bytes = static_cast<uint32_t>(payload_bytes.size());

  WriteDescriptorAndPayload(desc, payload_bytes);

  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetFormat(), oxygen::Format::kUnknown);
}

//! Test: missing payload magic triggers runtime error.
NOLINT_TEST_F(TextureLoaderBasicTestTest, LoadTexture_InvalidMagic_Throws)
{
  using oxygen::content::testing::MakeV4TexturePayload;
  using oxygen::data::pak::TextureResourceDesc;

  TextureResourceDesc desc {
    .data_offset = 256,
    .size_bytes = 0,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 8,
    .height = 8,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .content_hash = 0,
  };

  auto payload_bytes = MakeV4TexturePayload(32U, std::byte { 0x33 });
  desc.size_bytes = static_cast<uint32_t>(payload_bytes.size());
  payload_bytes[0] = 0x00; // corrupt magic

  WriteDescriptorAndPayload(desc, payload_bytes);

  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::invalid_argument);
}

//! Test: invalid descriptor dimensions still throw with a valid v4 payload.
NOLINT_TEST_F(TextureLoaderBasicTestTest, LoadTexture_InvalidDimensions_Throw)
{
  using oxygen::content::testing::MakeV4TexturePayload;
  using oxygen::data::pak::TextureResourceDesc;

  TextureResourceDesc desc {
    .data_offset = 256,
    .size_bytes = 0,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture1D),
    .compression_type = 0,
    .width = 0, // invalid
    .height = 1,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<uint8_t>(oxygen::Format::kR8SInt),
    .alignment = 256,
    .content_hash = 0,
  };

  const auto payload_bytes = MakeV4TexturePayload(16U, std::byte { 0x44 });
  desc.size_bytes = static_cast<uint32_t>(payload_bytes.size());

  WriteDescriptorAndPayload(desc, payload_bytes);

  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::invalid_argument);
}

//! Test: truncated payload (smaller than header) throws.
NOLINT_TEST_F(TextureLoaderBasicTestTest, LoadTexture_TruncatedPayload_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;

  TextureResourceDesc desc {
    .data_offset = 0,
    .size_bytes = 8, // smaller than header
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 4,
    .height = 4,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .content_hash = 0,
  };

  std::vector<uint8_t> payload(desc.size_bytes, 0);
  WriteDescriptorAndPayload(desc, payload);

  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::invalid_argument);
}

} // namespace
