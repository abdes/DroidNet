//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace core = oxygen::data::pak::core;
using oxygen::data::BufferElementStride;
using oxygen::data::BufferResourceDescriptor;
using oxygen::data::BufferResourceDescriptorRecord;
using oxygen::data::BufferResourceView;
using oxygen::data::BufferViewRecord;
using oxygen::data::DecodeBufferResourceDescriptor;
using oxygen::data::DecodeTextureResourceDescriptor;
using oxygen::data::EncodeBufferResourceDescriptor;
using oxygen::data::EncodeTextureResourceDescriptor;
using oxygen::data::kBufferResourceDescriptorMagic;
using oxygen::data::kBufferResourceDescriptorVersion;
using oxygen::data::kTextureResourceDescriptorMagic;
using oxygen::data::kTextureResourceDescriptorSize;
using oxygen::data::kTextureResourceDescriptorVersion;
using oxygen::data::kWholeBufferViewName;
using oxygen::data::ResourceDescriptorHeader;
using oxygen::data::TextureResourceDescriptor;
using oxygen::data::WholeBufferView;

//! Where a descriptor header stores its version: after the magic and index.
constexpr auto kVersionOffset
  = sizeof(kTextureResourceDescriptorMagic) + sizeof(core::ResourceIndexT);

//! Where the buffer descriptor's view `view` stores its name offset: the
//! last field of its record.
constexpr auto NameOffsetAt(const std::size_t view) -> std::size_t
{
  return sizeof(BufferResourceDescriptorRecord)
    + ((view + 1U) * sizeof(BufferViewRecord))
    - sizeof(core::StringTableOffsetT);
}

auto Texture() -> TextureResourceDescriptor
{
  return {
    .index = core::ResourceIndexT { 3U },
    .descriptor = core::TextureResourceDesc {
      .data_offset = 512U,
      .size_bytes = 64U,
      .texture_type = 2U,
      .compression_type = 0U,
      .width = 4U,
      .height = 4U,
      .depth = 1U,
      .array_layers = 1U,
      .mip_levels = 1U,
      .format = 7U,
      .alignment = 256U,
      .content_hash = { 0xAB },
    },
  };
}

auto WholeView(const uint64_t size) -> BufferResourceView
{
  return {
    .name = std::string(kWholeBufferViewName),
    .byte_offset = 0U,
    .byte_length = size,
    .element_offset = 0U,
    .element_count = size / 4U,
  };
}

auto Buffer() -> BufferResourceDescriptor
{
  return {
    .index = core::ResourceIndexT { 5U },
    .descriptor = core::BufferResourceDesc {
      .data_offset = 1024U,
      .size_bytes = 32U,
      .usage_flags = 1U,
      .element_stride = 4U,
      .element_format = 0U,
      .content_hash = { 0xCD },
    },
    .views = {
      WholeView(32U),
      BufferResourceView {
        .name = "tail",
        .byte_offset = 16U,
        .byte_length = 16U,
        .element_offset = 4U,
        .element_count = 4U,
      },
    },
  };
}

auto HeaderOf(const std::span<const std::byte> bytes)
  -> ResourceDescriptorHeader
{
  auto header = ResourceDescriptorHeader {};
  std::memcpy(&header, bytes.data(), sizeof(header));
  return header;
}

//! A texture descriptor is its header and the texture's row, and decodes to
//! what was encoded.
NOLINT_TEST(ResourceDescriptorTest, TextureRoundTrips)
{
  const auto texture = Texture();

  const auto bytes = EncodeTextureResourceDescriptor(texture);
  const auto decoded = DecodeTextureResourceDescriptor(bytes);

  ASSERT_EQ(bytes.size(), kTextureResourceDescriptorSize);
  const auto header = HeaderOf(bytes);
  EXPECT_EQ(header.magic, kTextureResourceDescriptorMagic);
  EXPECT_EQ(header.version, kTextureResourceDescriptorVersion);
  EXPECT_EQ(header.index, texture.index);
  ASSERT_HAS_VALUE(decoded);
  EXPECT_EQ(decoded->index, texture.index);
  EXPECT_EQ(std::memcmp(&decoded->descriptor, &texture.descriptor,
              sizeof(texture.descriptor)),
    0);
}

//! A texture descriptor of another size, magic or version does not decode.
NOLINT_TEST(ResourceDescriptorTest, TextureRejectsOtherLayouts)
{
  const auto bytes = EncodeTextureResourceDescriptor(Texture());
  auto longer = bytes;
  longer.push_back(std::byte { 0 });
  auto other_magic = bytes;
  other_magic.at(0) = std::byte { 'X' };
  auto other_version = bytes;
  other_version.at(kVersionOffset)
    = std::byte { kTextureResourceDescriptorVersion - 1U };

  EXPECT_FALSE(
    DecodeTextureResourceDescriptor(std::span(bytes).first(bytes.size() - 1U)));
  EXPECT_FALSE(DecodeTextureResourceDescriptor(longer));
  EXPECT_FALSE(DecodeTextureResourceDescriptor(other_magic));
  EXPECT_FALSE(DecodeTextureResourceDescriptor(other_version));
}

//! A buffer descriptor is its header, the buffer's row, its views and their
//! names, and decodes to what was encoded, views in order.
NOLINT_TEST(ResourceDescriptorTest, BufferRoundTrips)
{
  const auto buffer = Buffer();

  const auto bytes = EncodeBufferResourceDescriptor(buffer);
  const auto decoded = DecodeBufferResourceDescriptor(bytes);

  const auto header = HeaderOf(bytes);
  EXPECT_EQ(header.magic, kBufferResourceDescriptorMagic);
  EXPECT_EQ(header.version, kBufferResourceDescriptorVersion);
  EXPECT_EQ(header.index, buffer.index);
  // The string table: the empty string, then each name, `\0`-terminated.
  constexpr auto kStringsSize = 1U + sizeof("__all__") + sizeof("tail");
  EXPECT_EQ(bytes.size(),
    sizeof(BufferResourceDescriptorRecord) + (2U * sizeof(BufferViewRecord))
      + kStringsSize);
  ASSERT_HAS_VALUE(decoded);
  EXPECT_EQ(decoded->index, buffer.index);
  EXPECT_EQ(std::memcmp(&decoded->descriptor, &buffer.descriptor,
              sizeof(buffer.descriptor)),
    0);
  EXPECT_EQ(decoded->views, buffer.views);
}

//! Encoding refuses views a reader would reject: an empty or repeated name,
//! or no whole-buffer view.
NOLINT_TEST(ResourceDescriptorTest, BufferEncodeRejectsInvalidViews)
{
  auto unnamed = Buffer();
  unnamed.views.at(1).name.clear();
  auto repeated = Buffer();
  repeated.views.at(1).name = std::string(kWholeBufferViewName);
  auto partial = Buffer();
  partial.views.erase(partial.views.begin());

  NOLINT_EXPECT_THROW(
    (void)EncodeBufferResourceDescriptor(unnamed), std::invalid_argument);
  NOLINT_EXPECT_THROW(
    (void)EncodeBufferResourceDescriptor(repeated), std::invalid_argument);
  NOLINT_EXPECT_THROW(
    (void)EncodeBufferResourceDescriptor(partial), std::invalid_argument);
}

//! A buffer descriptor that is truncated, has trailing bytes, another magic
//! or version, a view name outside its table or a repeated view name does
//! not decode.
NOLINT_TEST(ResourceDescriptorTest, BufferRejectsMalformedBytes)
{
  const auto bytes = EncodeBufferResourceDescriptor(Buffer());
  auto longer = bytes;
  longer.push_back(std::byte { 0 });
  auto other_magic = bytes;
  other_magic.at(0) = std::byte { 'X' };
  auto other_version = bytes;
  other_version.at(kVersionOffset)
    = std::byte { kBufferResourceDescriptorVersion - 1U };
  auto stray_name = bytes;
  stray_name.at(NameOffsetAt(0U)) = std::byte { 0xFF };
  // The second view takes the first view's name.
  auto repeated_name = bytes;
  std::memcpy(&repeated_name.at(NameOffsetAt(1U)),
    &repeated_name.at(NameOffsetAt(0U)), sizeof(core::StringTableOffsetT));

  EXPECT_FALSE(DecodeBufferResourceDescriptor(
    std::span(bytes).first(sizeof(BufferResourceDescriptorRecord) - 1U)));
  EXPECT_FALSE(
    DecodeBufferResourceDescriptor(std::span(bytes).first(bytes.size() - 1U)));
  EXPECT_FALSE(DecodeBufferResourceDescriptor(longer));
  EXPECT_FALSE(DecodeBufferResourceDescriptor(other_magic));
  EXPECT_FALSE(DecodeBufferResourceDescriptor(other_version));
  EXPECT_FALSE(DecodeBufferResourceDescriptor(stray_name));
  EXPECT_FALSE(DecodeBufferResourceDescriptor(repeated_name));
}

//! A buffer's element is its stride, else its format's element, else a byte;
//! a format of no fixed per-element size has none.
NOLINT_TEST(ResourceDescriptorTest, BufferElementStrideFollowsTheDescriptor)
{
  const auto structured = core::BufferResourceDesc { .element_stride = 12U };
  const auto typed = core::BufferResourceDesc {
    .element_format = static_cast<uint8_t>(oxygen::Format::kR32Float),
  };
  const auto block_compressed = core::BufferResourceDesc {
    .element_format = static_cast<uint8_t>(oxygen::Format::kBC1UNorm),
  };

  EXPECT_EQ(BufferElementStride(structured), 12U);
  EXPECT_EQ(BufferElementStride(typed), 4U);
  EXPECT_EQ(BufferElementStride(core::BufferResourceDesc {}), 1U);
  EXPECT_EQ(BufferElementStride(block_compressed), 0U);
}

//! The whole-buffer view spans every byte and counts the buffer's elements.
NOLINT_TEST(ResourceDescriptorTest, WholeBufferViewSpansTheBuffer)
{
  const auto typed = core::BufferResourceDesc {
    .size_bytes = 32U,
    .element_format = static_cast<uint8_t>(oxygen::Format::kR32Float),
  };

  const auto view = WholeBufferView(typed);

  EXPECT_EQ(view,
    (BufferResourceView {
      .name = std::string(kWholeBufferViewName),
      .byte_offset = 0U,
      .byte_length = 32U,
      .element_offset = 0U,
      .element_count = 8U,
    }));
}

} // namespace
