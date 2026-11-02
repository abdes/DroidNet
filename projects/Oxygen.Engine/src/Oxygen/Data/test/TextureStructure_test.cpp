//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/TextureResource.h

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::cooker::testing::TextureBuilder;
using oxygen::cooker::testing::TextureRefusal;
using oxygen::data::CookedDefect;
using oxygen::data::pak::core::kTextureCompressionBc7;
using oxygen::data::pak::core::kTextureCompressionNone;
using oxygen::data::pak::core::TextureResourceDesc;
using oxygen::data::pak::render::SubresourceLayout;
using oxygen::data::pak::render::TexturePackingPolicyId;
using oxygen::data::pak::render::TexturePayloadFlags;
using oxygen::data::pak::render::TexturePayloadHeader;

//! A texture row declares a type, a format and extents the format defines,
//! and its payload lays out every subresource with at least the row bytes
//! of its format and exactly the bytes its rows hold.
class TextureStructureTest : public testing::Test {
protected:
  //! An 8x8 RGBA8 2D texture with its full chain of four mips.
  [[nodiscard]] static auto Texture() -> TextureBuilder
  {
    return TextureBuilder {}.Size({ .width = 8U, .height = 8U }).Mips(4U);
  }

  //! The refusal of `texture`.
  [[nodiscard]] static auto Refusal(const TextureBuilder& texture)
    -> std::optional<CookedDefect>
  {
    const auto payload = texture.Payload();
    return TextureRefusal(texture.Record(payload), payload);
  }

  //! The refusal of `Texture()` with its row changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto RecordRefusal(const Edit& edit)
    -> std::optional<CookedDefect>
  {
    const auto texture = Texture();
    const auto payload = texture.Payload();
    auto record = texture.Record(payload);
    edit(record);
    return TextureRefusal(record, payload);
  }

  //! The refusal of `Texture()` with its payload header changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto HeaderRefusal(const Edit& edit)
    -> std::optional<CookedDefect>
  {
    const auto texture = Texture();
    auto payload = texture.Payload();
    auto header = TextureBuilder::HeaderAt(payload);
    edit(header);
    TextureBuilder::SetHeader(payload, header);
    return TextureRefusal(texture.Record(payload), payload);
  }

  //! The refusal of `Texture()` with the layout of its mip 1 changed by
  //! `edit`.
  template <typename Edit>
  [[nodiscard]] static auto LayoutRefusal(const Edit& edit)
    -> std::optional<CookedDefect>
  {
    const auto texture = Texture();
    auto payload = texture.Payload();
    auto layout = TextureBuilder::SubresourceAt(payload, 1U);
    edit(layout);
    TextureBuilder::SetSubresource(payload, 1U, layout);
    return TextureRefusal(texture.Record(payload), payload);
  }
};

NOLINT_TEST_F(TextureStructureTest, ReadsWellFormedTextures)
{
  const auto textures = std::vector {
    Texture(),
    Texture().Policy(TexturePackingPolicyId::kTightPacked),
    TextureBuilder {}
      .PixelFormat(Format::kBC7UNormSRGB)
      .Size({ .width = 8U, .height = 8U })
      .Mips(4U),
    TextureBuilder {}
      .PixelFormat(Format::kBC7UNorm)
      .Size({ .width = 5U, .height = 3U })
      .Mips(3U),
    TextureBuilder {}
      .Type(TextureType::kTexture1D)
      .Size({ .width = 16U, .height = 1U })
      .Mips(5U),
    TextureBuilder {}
      .Type(TextureType::kTexture1DArray)
      .Size({ .width = 4U, .height = 1U })
      .Layers(3U),
    TextureBuilder {}
      .Type(TextureType::kTexture2DArray)
      .Size({ .width = 4U, .height = 2U })
      .Layers(3U),
    TextureBuilder {}
      .Type(TextureType::kTextureCube)
      .Size({ .width = 4U, .height = 4U })
      .Layers(6U),
    TextureBuilder {}
      .Type(TextureType::kTextureCubeArray)
      .Size({ .width = 2U, .height = 2U })
      .Layers(12U),
    TextureBuilder {}
      .Type(TextureType::kTexture3D)
      .Size({ .width = 4U, .height = 4U, .depth = 4U })
      .Mips(3U),
    TextureBuilder {}
      .PixelFormat(Format::kRGBA32Float)
      .Size({ .width = 3U, .height = 5U })
      .Mips(3U),
  };

  for (const auto& texture : textures) {
    EXPECT_EQ(Refusal(texture), std::nullopt);
  }
}

NOLINT_TEST_F(TextureStructureTest, ReadsPremultipliedAlpha)
{
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.flags
      = static_cast<uint8_t>(TexturePayloadFlags::kPremultipliedAlpha);
  }),
    std::nullopt);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnUnknownType)
{
  for (const auto type : { uint8_t { 0U }, uint8_t { 200U } }) {
    EXPECT_EQ(RecordRefusal([type](TextureResourceDesc& record) -> void {
      record.texture_type = type;
    }),
      CookedDefect::kInvalidValue);
  }
}

//! A row has no sample count, so it cannot describe a multisampled texture.
NOLINT_TEST_F(TextureStructureTest, RefusesAMultisampledTexture)
{
  for (const auto type : {
         TextureType::kTexture2DMultiSample,
         TextureType::kTexture2DMultiSampleArray,
       }) {
    EXPECT_EQ(
      Refusal(TextureBuilder {}.Type(type)), CookedDefect::kInvalidValue);
  }
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnUnknownFormat)
{
  for (const auto format : { uint8_t { 0U }, uint8_t { 255U } }) {
    EXPECT_EQ(RecordRefusal([format](TextureResourceDesc& record) -> void {
      record.format = format;
    }),
      CookedDefect::kInvalidValue);
  }
}

NOLINT_TEST_F(TextureStructureTest, RefusesACompressionItsFormatDoesNotUse)
{
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.compression_type = kTextureCompressionBc7;
  }),
    CookedDefect::kInvalidValue);

  const auto bc7 = TextureBuilder {}
                     .PixelFormat(Format::kBC7UNorm)
                     .Size({ .width = 4U, .height = 4U });
  const auto payload = bc7.Payload();
  auto record = bc7.Record(payload);
  record.compression_type = kTextureCompressionNone;
  EXPECT_EQ(TextureRefusal(record, payload), CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnEmptyExtent)
{
  EXPECT_EQ(RecordRefusal(
              [](TextureResourceDesc& record) -> void { record.width = 0U; }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(RecordRefusal(
              [](TextureResourceDesc& record) -> void { record.height = 0U; }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(RecordRefusal(
              [](TextureResourceDesc& record) -> void { record.depth = 0U; }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.array_layers = 0U;
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.mip_levels = 0U;
  }),
    CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(TextureStructureTest, RefusesExtentsItsTypeDoesNotHave)
{
  const auto shapes = std::vector {
    TextureBuilder {}
      .Type(TextureType::kTexture1D)
      .Size({ .width = 4U, .height = 2U }),
    TextureBuilder {}
      .Type(TextureType::kTexture1D)
      .Size({ .width = 4U, .height = 1U })
      .Layers(2U),
    TextureBuilder {}
      .Type(TextureType::kTexture1DArray)
      .Size({ .width = 4U, .height = 1U, .depth = 2U }),
    TextureBuilder {}.Size({ .width = 4U, .height = 4U, .depth = 2U }),
    TextureBuilder {}.Size({ .width = 4U, .height = 4U }).Layers(2U),
    TextureBuilder {}
      .Type(TextureType::kTexture2DArray)
      .Size({ .width = 4U, .height = 4U, .depth = 2U }),
    TextureBuilder {}
      .Type(TextureType::kTextureCube)
      .Size({ .width = 4U, .height = 4U })
      .Layers(5U),
    TextureBuilder {}
      .Type(TextureType::kTextureCube)
      .Size({ .width = 4U, .height = 4U })
      .Layers(12U),
    TextureBuilder {}
      .Type(TextureType::kTextureCube)
      .Size({ .width = 4U, .height = 2U })
      .Layers(6U),
    TextureBuilder {}
      .Type(TextureType::kTextureCubeArray)
      .Size({ .width = 4U, .height = 4U })
      .Layers(7U),
    TextureBuilder {}
      .Type(TextureType::kTexture3D)
      .Size({ .width = 4U, .height = 4U, .depth = 4U })
      .Layers(2U),
  };

  for (const auto& shape : shapes) {
    EXPECT_EQ(Refusal(shape), CookedDefect::kInconsistent);
  }
}

NOLINT_TEST_F(TextureStructureTest, RefusesMoreMipsThanItsChainHas)
{
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.mip_levels = 5U;
  }),
    CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnAlignmentOtherThanTheFormats)
{
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.alignment = 512U;
  }),
    CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAPayloadOfAnotherSize)
{
  EXPECT_EQ(RecordRefusal([](TextureResourceDesc& record) -> void {
    record.size_bytes += 1U;
  }),
    CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAPayloadShorterThanItsHeader)
{
  const auto texture = Texture();
  auto payload = texture.Payload();
  payload.resize(sizeof(TexturePayloadHeader) - 1U);

  EXPECT_EQ(
    TextureRefusal(texture.Record(payload), payload), CookedDefect::kTruncated);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnotherMagic)
{
  EXPECT_EQ(HeaderRefusal(
              [](TexturePayloadHeader& header) -> void { header.magic += 1U; }),
    CookedDefect::kWrongMagic);
}

NOLINT_TEST_F(TextureStructureTest, RefusesAnUnknownPackingPolicy)
{
  for (const auto policy : { uint8_t { 0U }, uint8_t { 3U } }) {
    EXPECT_EQ(HeaderRefusal([policy](TexturePayloadHeader& header) -> void {
      header.packing_policy = policy;
    }),
      CookedDefect::kInvalidValue);
  }
}

//! Uncompressed tail mips are reserved: no reader lays them out.
NOLINT_TEST_F(TextureStructureTest, RefusesUnknownOrReservedFlags)
{
  for (const auto flags : {
         static_cast<uint8_t>(TexturePayloadFlags::kTailMipsUncompressed),
         uint8_t { 0x80U },
       }) {
    EXPECT_EQ(HeaderRefusal([flags](TexturePayloadHeader& header) -> void {
      header.flags = flags;
    }),
      CookedDefect::kInvalidValue);
  }
}

NOLINT_TEST_F(TextureStructureTest, RefusesASubresourceCountOtherThanItsRows)
{
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.subresource_count -= 1U;
  }),
    CookedDefect::kInconsistent);
}

NOLINT_TEST_F(TextureStructureTest, RefusesATotalSizeOtherThanThePayloads)
{
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.total_payload_size += 1U;
  }),
    CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(TextureStructureTest, RefusesALayoutTableOutsideThePayload)
{
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.layouts_offset_bytes = sizeof(TexturePayloadHeader) - 1U;
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.layouts_offset_bytes = header.total_payload_size;
  }),
    CookedDefect::kOutOfBounds);
}

NOLINT_TEST_F(TextureStructureTest, RefusesDataThatOverlapsTheLayoutTable)
{
  EXPECT_EQ(HeaderRefusal([](TexturePayloadHeader& header) -> void {
    header.data_offset_bytes = header.layouts_offset_bytes;
  }),
    CookedDefect::kOutOfBounds);
}

//! Mip 1 of the 8x8 RGBA8 texture is 4 texels wide: 16 bytes a row.
NOLINT_TEST_F(TextureStructureTest, RefusesARowPitchBelowTheFormatsRowBytes)
{
  EXPECT_EQ(LayoutRefusal([](SubresourceLayout& layout) -> void {
    layout.row_pitch_bytes = 15U;
    layout.size_bytes = 15U * 4U;
  }),
    CookedDefect::kInconsistent);
}

NOLINT_TEST_F(TextureStructureTest, RefusesASizeOtherThanItsRowsHold)
{
  EXPECT_EQ(LayoutRefusal([](SubresourceLayout& layout) -> void {
    layout.size_bytes -= 1U;
  }),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(LayoutRefusal([](SubresourceLayout& layout) -> void {
    layout.size_bytes += 1U;
  }),
    CookedDefect::kSizeMismatch);
}

//! A 3D subresource holds every depth slice of its mip.
NOLINT_TEST_F(TextureStructureTest, RefusesAVolumeSubresourceWithoutItsSlices)
{
  const auto volume = TextureBuilder {}
                        .Type(TextureType::kTexture3D)
                        .Size({ .width = 4U, .height = 4U, .depth = 4U });
  auto payload = volume.Payload();
  auto layout = TextureBuilder::SubresourceAt(payload, 0U);
  layout.size_bytes = layout.row_pitch_bytes * 4U;
  TextureBuilder::SetSubresource(payload, 0U, layout);

  EXPECT_EQ(TextureRefusal(volume.Record(payload), payload),
    CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(TextureStructureTest, RefusesASubresourceOutsideTheData)
{
  EXPECT_EQ(LayoutRefusal([](SubresourceLayout& layout) -> void {
    layout.offset_bytes = 0xFFFF'FF00U;
  }),
    CookedDefect::kOutOfBounds);
}

} // namespace
