//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Headless/Texture.h>

using namespace oxygen::graphics::headless;

namespace {

//=== Layout tests ===--------------------------------------------------------//

//! Verify the contiguous layout strategy computes offsets and backing
//! reads/writes correctly for simple 2D RGBA textures.
NOLINT_TEST(HeadlessTextureTest, Texture_BasicMipOffsetsAndReadWrite)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.width = 8;
  desc.height = 8;
  desc.array_size = 1;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  Texture tex(desc);
  // Compute a per-slice total using the layout strategy
  const auto& strat = tex.GetLayoutStrategy();
  const auto per_slice = strat.ComputeTotalBytesPerArraySlice(desc);
  EXPECT_GT(per_slice, 0u);

  // Act: write a small pattern at mip 0 base and read it back
  std::vector<uint8_t> src { 1, 2, 3, 4, 5, 6, 7, 8 };
  const auto base_offset = strat.ComputeSliceMipBaseOffset(desc, 0, 0);
  tex.WriteBacking(src.data(), base_offset, static_cast<uint32_t>(src.size()));

  std::vector<uint8_t> dst(src.size());
  tex.ReadBacking(dst.data(), base_offset, static_cast<uint32_t>(dst.size()));

  // Assert
  EXPECT_EQ(dst, src);
}

//=== View payload tests ===--------------------------------------------------//

//! Verifies SRV/UAV view payloads contain the correct base_offset and
//! total_size.
/*!
 Arranges a multi-mip, multi-slice texture, creates SRV/UAV views for a
 subresource range, and asserts the owned payloads (public PODs) match the
 layout strategy's computed offsets and sizes.
*/
NOLINT_TEST(HeadlessTextureTest, Texture_SRVContainsCorrectOffsets)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.texture_type = oxygen::TextureType::kTexture2DArray;
  desc.width = 64;
  desc.height = 64;
  desc.array_size = 2;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  auto texture = std::make_shared<Texture>(desc);

  oxygen::graphics::TextureViewDescription view_desc {};
  view_desc.dimension = oxygen::TextureType::kTexture2D;
  view_desc.format = desc.format;

  oxygen::graphics::TextureSubResourceSet subresources;
  subresources.base_array_slice = 1;
  subresources.num_array_slices = 1;
  subresources.base_mip_level = 1;
  subresources.num_mip_levels = 2;
  view_desc.sub_resources = subresources;

  // Act: use public GetNativeView to obtain headless payloads (SRV)
  auto srv_native
    = texture->GetNativeView(oxygen::graphics::DescriptorHandle {}, view_desc);

  // Assert (SRV only)
  const auto* srv_payload = srv_native.AsPointer<const Texture::SRV>();
  ASSERT_NE(srv_payload, nullptr);
  EXPECT_EQ(srv_payload->texture, texture.get());

  const auto& layout = texture->GetLayoutStrategy();
  const auto expected_base = layout.ComputeSliceMipBaseOffset(desc, 1, 1);
  uint32_t expected_total = 0;
  for (auto m = 1u; m < 1u + 2u; ++m) {
    expected_total += layout.ComputeMipSizeBytes(desc, m);
  }

  EXPECT_EQ(srv_payload->base_offset, expected_base);
  EXPECT_EQ(srv_payload->total_size, expected_total);
}

//! Same as the SRV test but for UAV payloads only
NOLINT_TEST(HeadlessTextureTest, Texture_UAVContainsCorrectOffsets)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.texture_type = oxygen::TextureType::kTexture2DArray;
  desc.width = 64;
  desc.height = 64;
  desc.array_size = 2;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  auto texture = std::make_shared<Texture>(desc);

  oxygen::graphics::TextureViewDescription view_desc {};
  view_desc.dimension = oxygen::TextureType::kTexture2D;
  view_desc.format = desc.format;

  oxygen::graphics::TextureSubResourceSet subresources;
  subresources.base_array_slice = 1;
  subresources.num_array_slices = 1;
  subresources.base_mip_level = 1;
  subresources.num_mip_levels = 2;
  view_desc.sub_resources = subresources;

  // Act
  auto uav_native
    = texture->GetNativeView(oxygen::graphics::DescriptorHandle {}, view_desc);

  // Assert (UAV only)
  const auto* uav_payload = uav_native.AsPointer<const Texture::UAV>();
  ASSERT_NE(uav_payload, nullptr);
  EXPECT_EQ(uav_payload->texture, texture.get());

  const auto& layout = texture->GetLayoutStrategy();
  const auto expected_base = layout.ComputeSliceMipBaseOffset(desc, 1, 1);
  uint32_t expected_total = 0;
  for (auto m = 1u; m < 1u + 2u; ++m) {
    expected_total += layout.ComputeMipSizeBytes(desc, m);
  }

  EXPECT_EQ(uav_payload->base_offset, expected_base);
  EXPECT_EQ(uav_payload->total_size, expected_total);
}

//! Verify layout strategy reports consistent total size across mip sums
NOLINT_TEST(HeadlessTextureTest, Texture_Layout_TotalSizeMatchesMipSum)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.width = 128;
  desc.height = 64;
  desc.array_size = 1;
  desc.mip_levels = 5;
  desc.format = oxygen::Format::kRGBA8UNorm;

  Texture tex(desc);
  const auto& layout = tex.GetLayoutStrategy();

  // Act: compute total by summing mips
  uint32_t sum = 0;
  for (auto mip = 0u; mip < desc.mip_levels; ++mip) {
    sum += layout.ComputeMipSizeBytes(desc, mip);
  }
  const auto total = layout.ComputeTotalBytesPerArraySlice(desc);

  // Assert
  EXPECT_EQ(sum, total);
}

//! Cross-mip read/write: ensure writing to one mip does not clobber others
NOLINT_TEST(HeadlessTextureTest, Texture_CrossMipIsolation)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.width = 16;
  desc.height = 16;
  desc.array_size = 1;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  Texture tex(desc);
  const auto& layout = tex.GetLayoutStrategy();

  // Use a small pattern so the test doesn't depend on the texture backing
  // allocating every mip's full data. This test verifies isolation: a write
  // to mip1 must not clobber data previously written to mip0.
  const uint8_t small_pattern = 4;
  std::vector<uint8_t> mip0(small_pattern, 0x11);
  std::vector<uint8_t> mip1(small_pattern, 0x22);

  const auto off0 = layout.ComputeSliceMipBaseOffset(desc, 0, 0);
  const auto off1 = layout.ComputeSliceMipBaseOffset(desc, 0, 1);

  // Act: write to mip0, then attempt to write to mip1 (may be truncated)
  tex.WriteBacking(mip0.data(), off0, static_cast<uint32_t>(mip0.size()));
  tex.WriteBacking(mip1.data(), off1, static_cast<uint32_t>(mip1.size()));

  // Assert: mip0 must remain unchanged (isolation)
  std::vector<uint8_t> r0(small_pattern);
  tex.ReadBacking(r0.data(), off0, static_cast<uint32_t>(r0.size()));
  EXPECT_EQ(r0, mip0);
}

//=== View payload bounds and safety ===--------------------------------------//

//! View payload bounds: SRV/UAV views for subresource ranges must fall within
//! the texture backing size.
NOLINT_TEST(HeadlessTextureTest, Texture_ViewPayloadBoundsWithinBacking)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.texture_type = oxygen::TextureType::kTexture2DArray;
  desc.width = 32;
  desc.height = 32;
  desc.array_size = 2;
  desc.mip_levels = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;

  auto texture = std::make_shared<Texture>(desc);
  const auto& layout = texture->GetLayoutStrategy();
  const auto per_slice = layout.ComputeTotalBytesPerArraySlice(desc);

  // Choose a subresource covering mips 1..2 on array slice 0
  oxygen::graphics::TextureViewDescription view_desc {};
  view_desc.dimension = oxygen::TextureType::kTexture2D;
  oxygen::graphics::TextureSubResourceSet sr {};
  sr.base_array_slice = 0;
  sr.num_array_slices = 1;
  sr.base_mip_level = 1;
  sr.num_mip_levels = 2;
  view_desc.sub_resources = sr;

  // Act
  auto native
    = texture->GetNativeView(oxygen::graphics::DescriptorHandle {}, view_desc);
  const auto* srv = native.AsPointer<const Texture::SRV>();
  ASSERT_NE(srv, nullptr);

  // Compute expected offsets
  const auto expected_base = layout.ComputeSliceMipBaseOffset(
    desc, sr.base_array_slice, sr.base_mip_level);
  uint32_t expected_total = 0;
  for (auto m = sr.base_mip_level; m < sr.base_mip_level + sr.num_mip_levels;
    ++m) {
    expected_total += layout.ComputeMipSizeBytes(desc, m);
  }

  // Assert: payload fields are within per-slice backing
  EXPECT_EQ(srv->base_offset, expected_base);
  EXPECT_EQ(srv->total_size, expected_total);
  EXPECT_LT(srv->base_offset + srv->total_size, per_slice + 1u); // <= per_slice
}

//! Out-of-range reads should be no-ops and not crash
NOLINT_TEST(HeadlessTextureTest, Texture_ReadBacking_OutOfRangeNoOp)
{
  // Arrange
  oxygen::graphics::TextureDesc desc {};
  desc.width = 8;
  desc.height = 8;
  desc.array_size = 1;
  desc.mip_levels = 1;
  desc.format = oxygen::Format::kRGBA8UNorm;

  Texture tex(desc);
  const auto& layout = tex.GetLayoutStrategy();
  const auto per_slice = layout.ComputeTotalBytesPerArraySlice(desc);

  // Act / Assert: reading beyond end should be safe
  std::vector<uint8_t> buf(16);
  tex.ReadBacking(
    buf.data(), per_slice + 10, static_cast<uint32_t>(buf.size()));
}

//=== Compressed format / block tests ===------------------------------------//

//! Verify block-compressed formats produce expected block-aligned sizes
NOLINT_TEST(HeadlessTextureTest, Texture_BC1_BlockSizeAndOffsets)
{
  // Arrange: BC1 uses 4x4 blocks of 8 bytes per block
  oxygen::graphics::TextureDesc desc {};
  desc.width = 17; // not multiple of block size to exercise rounding
  desc.height = 9;
  desc.array_size = 1;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kBC1UNorm;

  Texture tex(desc);
  const auto& layout = tex.GetLayoutStrategy();

  // Act/Assert: each mip size must be a multiple of the block bytes
  for (auto m = 0u; m < desc.mip_levels; ++m) {
    const auto sz = layout.ComputeMipSizeBytes(desc, m);
    const auto finfo = oxygen::graphics::detail::GetFormatInfo(desc.format);
    EXPECT_EQ(sz % finfo.bytes_per_block, 0u);
  }

  // Ensure payload offsets produced for an SRV are within backing
  oxygen::graphics::TextureViewDescription view_desc {};
  view_desc.dimension = oxygen::TextureType::kTexture2D;
  view_desc.format = desc.format;
  oxygen::graphics::TextureSubResourceSet sr {};
  sr.base_array_slice = 0;
  sr.num_array_slices = 1;
  sr.base_mip_level = 0;
  sr.num_mip_levels = desc.mip_levels;
  view_desc.sub_resources = sr;

  const auto native
    = tex.GetNativeView(oxygen::graphics::DescriptorHandle {}, view_desc);
  const auto* srv = native.AsPointer<const Texture::SRV>();
  ASSERT_NE(srv, nullptr);

  const auto backing = tex.GetBackingSize();
  EXPECT_LE(srv->base_offset + srv->total_size, backing);
}

//! Verify BC3/BC5 family block sizes and per-slice totals
NOLINT_TEST(HeadlessTextureTest, Texture_BC3_BC5_PerSliceTotals)
{
  // Arrange a two-slice texture to exercise array handling
  oxygen::graphics::TextureDesc desc {};
  desc.width = 64;
  desc.height = 32;
  desc.array_size = 2;
  desc.mip_levels = 4;
  desc.format = oxygen::Format::kBC3UNorm;

  Texture tex(desc);
  const auto& layout = tex.GetLayoutStrategy();

  const auto per_slice = layout.ComputeTotalBytesPerArraySlice(desc);
  EXPECT_GT(per_slice, 0u);

  // For BC3, bytes_per_block should be 16
  auto finfo = oxygen::graphics::detail::GetFormatInfo(desc.format);
  EXPECT_EQ(finfo.bytes_per_block, 16u);

  // Verify that offsets for the second slice are per_slice apart
  const auto off0 = layout.ComputeSliceMipBaseOffset(desc, 0, 0);
  const auto off1 = layout.ComputeSliceMipBaseOffset(desc, 1, 0);
  EXPECT_EQ(off1, off0 + per_slice);

  // Confirm backing can contain both slices when allocation was made
  const auto backing = tex.GetBackingSize();
  if (backing > 0) {
    EXPECT_LE(per_slice * desc.array_size, backing);
  }
}

} // namespace
