//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedOpenError.h>
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
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
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

//! The rule a decode that must fail reports broken.
template <class T>
auto DefectOf(const oxygen::Result<T, CookedOpenError>& decoded) -> CookedDefect
{
  if (decoded) {
    ADD_FAILURE() << "the descriptor decoded";
    return {};
  }
  return decoded.error().Defect();
}

//! Writes `value` over the field at `offset` of `bytes`.
auto Patch(std::vector<std::byte>& bytes, const std::size_t offset,
  const uint64_t value) -> void
{
  std::memcpy(&bytes.at(offset), &value, sizeof(value));
}

//! Where the buffer descriptor's view `view` stores the field at
//! `field_offset` of its record.
constexpr auto ViewFieldAt(
  const std::size_t view, const std::size_t field_offset) -> std::size_t
{
  return sizeof(BufferResourceDescriptorRecord)
    + (view * sizeof(BufferViewRecord)) + field_offset;
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

//! A texture descriptor of another magic is another format, of another
//! version unsupported, and of another size malformed.
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

  EXPECT_EQ(DefectOf(DecodeTextureResourceDescriptor(
              std::span(bytes).first(sizeof(ResourceDescriptorHeader) - 1U))),
    CookedDefect::kTooSmall);
  EXPECT_EQ(DefectOf(DecodeTextureResourceDescriptor(
              std::span(bytes).first(bytes.size() - 1U))),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(DefectOf(DecodeTextureResourceDescriptor(longer)),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(DefectOf(DecodeTextureResourceDescriptor(other_magic)),
    CookedDefect::kWrongMagic);
  EXPECT_EQ(DefectOf(DecodeTextureResourceDescriptor(other_version)),
    CookedDefect::kUnsupportedVersion);
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

//! Views at the buffer's bounds are valid: one that ends at its last byte,
//! and an empty one at its end.
NOLINT_TEST(ResourceDescriptorTest, BufferRoundTripsBoundaryViews)
{
  auto buffer = Buffer();
  buffer.views.push_back({
    .name = "end",
    .byte_offset = 32U,
    .byte_length = 0U,
    .element_offset = 8U,
    .element_count = 0U,
  });

  const auto decoded
    = DecodeBufferResourceDescriptor(EncodeBufferResourceDescriptor(buffer));

  ASSERT_HAS_VALUE(decoded);
  EXPECT_EQ(decoded->views, buffer.views);
}

//! Encoding refuses views a reader would reject: an empty or repeated name,
//! no whole-buffer view, a whole-buffer view that does not span the buffer,
//! and a view outside the buffer, off its stride or whose elements are not
//! its bytes.
NOLINT_TEST(ResourceDescriptorTest, BufferEncodeRejectsInvalidViews)
{
  const auto with_tail = [](const auto& change) -> BufferResourceDescriptor {
    auto buffer = Buffer();
    change(buffer.views.at(1));
    return buffer;
  };
  auto partial = Buffer();
  partial.views.erase(partial.views.begin());
  auto short_whole = Buffer();
  short_whole.views.at(0).byte_length = 16U;
  const auto invalid = std::array {
    with_tail([](BufferResourceView& view) -> void { view.name.clear(); }),
    with_tail([](BufferResourceView& view) -> void {
      view.name = std::string(kWholeBufferViewName);
    }),
    partial,
    short_whole,
    // Overflows when its end is computed naively.
    with_tail([](BufferResourceView& view) -> void {
      view.byte_offset = std::numeric_limits<uint64_t>::max();
      view.byte_length = 2U;
    }),
    with_tail([](BufferResourceView& view) -> void { view.byte_length = 20U; }),
    with_tail([](BufferResourceView& view) -> void {
      view.byte_offset = 18U;
      view.byte_length = 12U;
    }),
    with_tail(
      [](BufferResourceView& view) -> void { view.element_count = 3U; }),
  };

  for (const auto& buffer : invalid) {
    NOLINT_EXPECT_THROW(
      (void)EncodeBufferResourceDescriptor(buffer), std::invalid_argument);
  }
}

//! A view a writer would refuse is malformed when read: outside the buffer,
//! even by overflow, off its stride, or of elements other than its bytes.
NOLINT_TEST(ResourceDescriptorTest, BufferRejectsInvalidViewRanges)
{
  const auto bytes = EncodeBufferResourceDescriptor(Buffer());
  const auto with = [&bytes](const std::size_t field_offset,
                      const uint64_t value) -> std::vector<std::byte> {
    auto broken = bytes;
    Patch(broken, ViewFieldAt(1U, field_offset), value);
    return broken;
  };
  struct Case {
    std::vector<std::byte> bytes;
    CookedDefect defect;
  };
  const auto cases = std::array {
    Case {
      .bytes = with(offsetof(BufferViewRecord, byte_offset),
        std::numeric_limits<uint64_t>::max()),
      .defect = CookedDefect::kOutOfBounds,
    },
    Case {
      .bytes = with(offsetof(BufferViewRecord, byte_length), 20U),
      .defect = CookedDefect::kOutOfBounds,
    },
    Case {
      .bytes = with(offsetof(BufferViewRecord, byte_length), 14U),
      .defect = CookedDefect::kMisaligned,
    },
    Case {
      .bytes = with(offsetof(BufferViewRecord, element_offset), 3U),
      .defect = CookedDefect::kInconsistent,
    },
  };

  for (const auto& broken : cases) {
    EXPECT_EQ(
      DefectOf(DecodeBufferResourceDescriptor(broken.bytes)), broken.defect);
  }
}

//! A buffer descriptor of another magic is another format, of another
//! version unsupported; one that is truncated, has trailing bytes, a view
//! name outside its table or a repeated view name is malformed.
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

  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(std::span(bytes).first(
              sizeof(BufferResourceDescriptorRecord) - 1U))),
    CookedDefect::kTruncated);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(
              std::span(bytes).first(bytes.size() - 1U))),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(longer)),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(other_magic)),
    CookedDefect::kWrongMagic);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(other_version)),
    CookedDefect::kUnsupportedVersion);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(stray_name)),
    CookedDefect::kInvalidString);
  EXPECT_EQ(DefectOf(DecodeBufferResourceDescriptor(repeated_name)),
    CookedDefect::kDuplicate);
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
