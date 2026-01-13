//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/TextureResource.h>

using oxygen::data::TextureResource;

namespace {

[[nodiscard]] auto MakeValidPayload(uint16_t array_layers, uint16_t mip_levels,
  uint32_t bytes_per_subresource, uint64_t content_hash) -> std::vector<uint8_t>
{
  using oxygen::data::pak::SubresourceLayout;
  using oxygen::data::pak::TexturePayloadHeader;

  const auto subresource_count = static_cast<uint16_t>(
    static_cast<uint32_t>(array_layers) * static_cast<uint32_t>(mip_levels));

  TexturePayloadHeader header {};
  header.subresource_count = subresource_count;
  header.layouts_offset_bytes = sizeof(TexturePayloadHeader);
  header.data_offset_bytes = sizeof(TexturePayloadHeader)
    + static_cast<uint32_t>(subresource_count) * sizeof(SubresourceLayout);
  header.content_hash = content_hash;
  header.total_payload_size
    = header.data_offset_bytes + subresource_count * bytes_per_subresource;

  std::vector<uint8_t> payload(header.total_payload_size, 0);
  std::memcpy(payload.data(), &header, sizeof(header));

  for (uint16_t i = 0; i < subresource_count; ++i) {
    const SubresourceLayout layout {
      .offset_bytes = static_cast<uint32_t>(i) * bytes_per_subresource,
      .row_pitch_bytes = bytes_per_subresource,
      .size_bytes = bytes_per_subresource,
    };

    const auto offset = static_cast<std::size_t>(header.layouts_offset_bytes)
      + static_cast<std::size_t>(i) * sizeof(SubresourceLayout);
    std::memcpy(payload.data() + offset, &layout, sizeof(layout));
  }

  return payload;
}

//! Basic test verifying TextureResource accessors return descriptor values (ID
//! 35).
NOLINT_TEST(TextureResourceBasicTest, AccessorsReturnDescriptorValues)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc desc {
    .data_offset = 4096,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 5,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };
  auto payload = MakeValidPayload(desc.array_layers, desc.mip_levels,
    /*bytes_per_subresource=*/4u, desc.content_hash);
  desc.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act
  TextureResource tex { desc, std::move(payload) };

  // Assert
  EXPECT_EQ(tex.GetDataOffset(), 4096u);
  EXPECT_EQ(tex.GetPayload().size(), desc.size_bytes);
  EXPECT_EQ(tex.GetDataSize(), 20u);
  EXPECT_EQ(tex.GetWidth(), 128u);
  EXPECT_EQ(tex.GetHeight(), 64u);
  EXPECT_EQ(tex.GetDepth(), 1u);
  EXPECT_EQ(tex.GetArrayLayers(), 1u);
  EXPECT_EQ(tex.GetMipCount(), 5u);
  EXPECT_EQ(tex.GetFormat(), oxygen::Format::kRGBA8UNorm);
  EXPECT_EQ(tex.GetDataAlignment(), 256u);
}

//! Death test verifying invalid descriptor (zero width) triggers invariant (ID
//! 36).
class TextureResourceValidationTest : public testing::Test { };

//! Move semantics transfer ownership of data buffer (additional coverage).
NOLINT_TEST_F(TextureResourceValidationTest, MoveConstructor_TransfersOwnership)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc desc {
    .data_offset = 1024,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };
  auto payload = MakeValidPayload(desc.array_layers, desc.mip_levels,
    /*bytes_per_subresource=*/8u, desc.content_hash);
  desc.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act
  TextureResource original { desc, std::move(payload) };
  auto moved = std::move(original);

  // Assert
  EXPECT_EQ(moved.GetDataSize(), 8u);
  EXPECT_EQ(original.GetDataSize(), 0u); // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(original.GetPayload().size(), 0u);
}

//! Invalid descriptor: zero width must throw.
NOLINT_TEST_F(TextureResourceValidationTest, InvalidDescriptor_ZeroWidth_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 0, // invalid
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };
  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: zero height for 2D texture must throw.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_ZeroHeight_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 16,
    .height = 0, // invalid
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: zero depth for 3D texture must throw.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_ZeroDepth3D_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture3D),
    .compression_type = 0,
    .width = 4,
    .height = 4,
    .depth = 0, // invalid
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: zero mip levels must throw.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_ZeroMipLevels_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 8,
    .height = 8,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 0, // invalid
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: excessive mip levels (greater than log2(max) + 1) must
//! throw.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_ExcessiveMipLevels_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 8,
    .height = 4,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 6, // invalid: max for 8 is 4 (8->1) so 4 + maybe 1? Actually
                     // log2(8)=3 +1=4
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: array layers must be >=1.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_ZeroArrayLayers_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type
    = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2DArray),
    .compression_type = 0,
    .width = 4,
    .height = 4,
    .depth = 1,
    .array_layers = 0, // invalid
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: data size mismatch between descriptor and buffer size.
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_DataSizeMismatch_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 4,
    .height = 4,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size() + 1);

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Invalid descriptor: alignment not 256 must throw (spec requires 256).
NOLINT_TEST_F(
  TextureResourceValidationTest, InvalidDescriptor_WrongAlignment_Throws)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc bad {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0,
    .width = 4,
    .height = 4,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 128, // invalid per spec
    .reserved = {},
  };

  auto payload = MakeValidPayload(bad.array_layers, bad.mip_levels,
    /*bytes_per_subresource=*/4u, bad.content_hash);
  bad.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act / Assert
  NOLINT_EXPECT_THROW(
    TextureResource(bad, std::move(payload)), std::invalid_argument);
}

//! Resiliency: invalid enumerant values map to kUnknown but do not throw.
NOLINT_TEST_F(
  TextureResourceValidationTest, Resiliency_InvalidEnums_MapToUnknownNoThrow)
{
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc weird {
    .data_offset = 0,
    .size_bytes = 0,
    .texture_type = 99, // out of range
    .compression_type = 0,
    .width = 1,
    .height = 1,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = 255, // out of range
    .alignment = 256,
    .reserved = {},
  };

  auto payload = MakeValidPayload(weird.array_layers, weird.mip_levels,
    /*bytes_per_subresource=*/1u, weird.content_hash);
  weird.size_bytes = static_cast<uint32_t>(payload.size());

  // Arrange

  // Act
  TextureResource tex { weird, std::move(payload) };

  // Assert
  EXPECT_EQ(tex.GetTextureType(), oxygen::TextureType::kUnknown);
  EXPECT_EQ(tex.GetFormat(), oxygen::Format::kUnknown);
}

} // namespace
