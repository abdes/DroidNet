//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/BufferResource.h

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::cooker::testing::BufferRefusal;
using oxygen::data::BufferResource;
using oxygen::data::CookedDefect;
using oxygen::data::CookedFormatError;
using oxygen::data::pak::core::BufferResourceDesc;
using Usage = oxygen::data::BufferResource::UsageFlags;

//! A buffer row declares an element format a buffer can hold or a stride,
//! not both, usage flags the format defines, and a size of whole elements.
class BufferStructureTest : public testing::Test {
protected:
  //! A static vertex buffer of `size` bytes of `stride`-byte elements.
  [[nodiscard]] static auto Structured(
    const uint32_t size, const uint32_t stride) -> BufferResourceDesc
  {
    return {
      .size_bytes = size,
      .usage_flags
      = static_cast<uint32_t>(Usage::kVertexBuffer | Usage::kStatic),
      .element_stride = stride,
    };
  }

  //! A static buffer of `size` bytes of `format` elements.
  [[nodiscard]] static auto Formatted(const uint32_t size, const Format format)
    -> BufferResourceDesc
  {
    return {
      .size_bytes = size,
      .usage_flags = static_cast<uint32_t>(Usage::kStatic),
      .element_format = static_cast<uint8_t>(format),
    };
  }

  //! The refusal of `desc` with a payload of its size.
  [[nodiscard]] static auto Refusal(const BufferResourceDesc& desc)
    -> std::optional<CookedDefect>
  {
    const auto payload = std::vector<std::byte>(desc.size_bytes);
    return BufferRefusal(desc, payload);
  }
};

NOLINT_TEST_F(BufferStructureTest, ReadsWellFormedBuffers)
{
  const auto buffers = std::vector {
    Structured(24U, 12U),
    Structured(7U, 1U),
    Structured(0U, 0U),
    Formatted(32U, Format::kRGBA32Float),
    Formatted(6U, Format::kR16UInt),
    BufferResourceDesc {
      .size_bytes = 8U,
      .usage_flags = static_cast<uint32_t>(Usage::kIndexBuffer
        | Usage::kCPUReadable | Usage::kCPUWritable | Usage::kDynamic),
      .element_stride = 4U,
    },
  };

  for (const auto& buffer : buffers) {
    EXPECT_EQ(Refusal(buffer), std::nullopt);
  }
}

NOLINT_TEST_F(BufferStructureTest, RefusesAnUnknownElementFormat)
{
  auto desc = Formatted(16U, Format::kRGBA32Float);
  desc.element_format = 255U;

  EXPECT_EQ(Refusal(desc), CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(BufferStructureTest, RefusesABlockCompressedElementFormat)
{
  EXPECT_EQ(
    Refusal(Formatted(16U, Format::kBC7UNorm)), CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(BufferStructureTest, RefusesAStrideOnAFormattedBuffer)
{
  auto desc = Formatted(16U, Format::kRGBA32Float);
  desc.element_stride = 16U;

  EXPECT_EQ(Refusal(desc), CookedDefect::kInconsistent);
}

NOLINT_TEST_F(BufferStructureTest, RefusesAPartialFormatElement)
{
  EXPECT_EQ(
    Refusal(Formatted(20U, Format::kRGBA32Float)), CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(BufferStructureTest, RefusesElementsWithoutAStride)
{
  EXPECT_EQ(Refusal(Structured(16U, 0U)), CookedDefect::kInvalidValue);
}

NOLINT_TEST_F(BufferStructureTest, RefusesAPartialElement)
{
  EXPECT_EQ(Refusal(Structured(3U, 4U)), CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(BufferStructureTest, RefusesUnknownUsageFlags)
{
  auto desc = Structured(16U, 4U);
  desc.usage_flags |= 0x400U;

  EXPECT_EQ(Refusal(desc), CookedDefect::kInvalidValue);
}

//! Dynamic, static and immutable are one update frequency each.
NOLINT_TEST_F(BufferStructureTest, RefusesMoreThanOneUpdateFrequency)
{
  for (const auto frequencies : {
         Usage::kDynamic | Usage::kStatic,
         Usage::kStatic | Usage::kImmutable,
         Usage::kDynamic | Usage::kImmutable,
       }) {
    auto desc = Structured(16U, 4U);
    desc.usage_flags = static_cast<uint32_t>(frequencies);

    EXPECT_EQ(Refusal(desc), CookedDefect::kInconsistent);
  }
}

NOLINT_TEST_F(BufferStructureTest, RefusesAPayloadOfAnotherSize)
{
  const auto desc = Structured(16U, 4U);
  const auto payload = std::vector<std::byte>(12U);

  EXPECT_EQ(BufferRefusal(desc, payload), CookedDefect::kSizeMismatch);

  auto refused = std::optional<CookedDefect> {};
  try {
    static_cast<void>(
      BufferResource(desc, oxygen::serio::ByteBuffer::CopyOf(payload).value()));
  } catch (const CookedFormatError& error) {
    refused = error.Defect();
  }
  EXPECT_EQ(refused, CookedDefect::kSizeMismatch);
}

} // namespace
