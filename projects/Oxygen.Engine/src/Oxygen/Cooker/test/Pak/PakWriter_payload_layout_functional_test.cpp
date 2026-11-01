//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "PakWriterTestSupport.h"

#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace paktest = oxygen::content::pak::test;

using oxygen::cooker::test::HasNoErrors;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;
using paktest::kFooterSize;
using paktest::kHeaderSize;

//! A texture table row of zeros: the writer patches its payload position.
auto BlankTextureDescriptor() -> std::vector<std::byte>
{
  return std::vector<std::byte>(
    sizeof(core::TextureResourceDesc), std::byte { 0 });
}

//! The `size` bytes of `bytes` at `offset`.
auto Slice(const std::span<const std::byte> bytes, const uint64_t offset,
  const uint64_t size) -> std::span<const std::byte>
{
  return bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(size));
}

auto IsAllZero(const std::span<const std::byte> bytes) -> bool
{
  return std::ranges::all_of(bytes,
    [](const std::byte byte) -> bool { return byte == std::byte { 0 }; });
}

//! The writer copies payload bytes verbatim and zero-fills everything the plan
//! leaves between them.
class PakWriterPayloadLayoutTest : public paktest::PakWriterFixture { };

NOLINT_TEST_F(PakWriterPayloadLayoutTest,
  StoresResourceAndDescriptorPayloadBytesFromSources)
{
  constexpr auto kContentVersion = uint16_t { 8U };
  constexpr auto kTexturePayloadSize = size_t { 8U };
  constexpr auto kDescriptorSize = size_t { 12U };
  const auto texture_payload = PatternBytes(kTexturePayloadSize);
  const auto descriptor = PatternBytes(kDescriptorSize, PatternSeed::kSecond);
  auto plan = CanonicalPlan({
    .content_version = kContentVersion,
    .texture_payload_size = kTexturePayloadSize,
  });
  plan.resource_payload_sources = { AddSource("texture.bin", texture_payload) };
  plan.resource_descriptor_sources
    = { AddSource("texture_descriptor.bin", BlankTextureDescriptor()) };
  paktest::AddGeometryAsset(
    plan, data::AssetKey::FromVirtualPath("/Game/Mesh.ogeo"), descriptor);
  plan.asset_payload_sources = { AddSource("mesh.ogeo", descriptor) };
  const auto texture_offset = plan.resources.front().offset;
  const auto descriptor_offset = plan.assets.front().offset;
  const auto planned_size = plan.planned_file_size;
  const auto request = Request(kContentVersion, "payload_copy.pak", false);

  const auto written = Write(request, std::move(plan));

  ASSERT_THAT(written.diagnostics, HasNoErrors());
  const auto bytes = Published(request);
  ASSERT_EQ(bytes.size(), planned_size);
  EXPECT_THAT(Slice(bytes, texture_offset, kTexturePayloadSize),
    BytesEqual(texture_payload));
  EXPECT_THAT(
    Slice(bytes, descriptor_offset, kDescriptorSize), BytesEqual(descriptor));
}

NOLINT_TEST_F(
  PakWriterPayloadLayoutTest, ZeroFillsPlannedPaddingAndTrailingGaps)
{
  constexpr auto kContentVersion = uint16_t { 2U };
  constexpr auto kTexturePayloadSize = size_t { 32U };
  constexpr auto kTrailingGap = uint64_t { 512U };
  auto plan = CanonicalPlan({
    .content_version = kContentVersion,
    .texture_payload_size = kTexturePayloadSize,
  });
  plan.resource_payload_sources
    = { AddSource("texture.bin", PatternBytes(kTexturePayloadSize)) };
  plan.resource_descriptor_sources
    = { AddSource("texture_descriptor.bin", BlankTextureDescriptor()) };
  // A gap after the footer, which the writer must zero-fill too.
  plan.planned_file_size += kTrailingGap;
  const auto texture_offset = plan.resources.front().offset;
  const auto texture_end = texture_offset + kTexturePayloadSize;
  // The texture region is padded to a payload boundary, where the tables start.
  const auto table_offset = plan.tables.front().offset;
  const auto planned_size = plan.planned_file_size;
  const auto footer_end = plan.footer.offset + kFooterSize;
  const auto request = Request(kContentVersion, "zero_fill.pak");

  const auto written = Write(request, std::move(plan));

  ASSERT_THAT(written.diagnostics, HasNoErrors());
  ASSERT_EQ(written.file_size, planned_size);
  const auto bytes = Published(request);
  ASSERT_EQ(bytes.size(), planned_size);
  EXPECT_TRUE(
    IsAllZero(Slice(bytes, kHeaderSize, texture_offset - kHeaderSize)));
  EXPECT_TRUE(IsAllZero(Slice(bytes, texture_end, table_offset - texture_end)));
  EXPECT_TRUE(IsAllZero(Slice(bytes, footer_end, kTrailingGap)));
}

} // namespace
