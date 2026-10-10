//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/TexturePipeline.cpp,
//   Import/Internal/TextureCooker.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/TextureCooker.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Test/Support/PipelineHarness.h>
#include <Oxygen/Cooker/Test/Support/TestImages.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::cooker::test::RunPipelineOnce;
namespace graphics_detail = oxygen::graphics::detail;

namespace {

//=== Test Utilities
//===---------------------------------------------------------//

using oxygen::cooker::test::MakeBmp;

//! Returns the test BMP image as a span of bytes.
[[nodiscard]] auto GetTestImageBytes() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp(2, 2);
  return { kTestBmp.data(), kTestBmp.size() };
}

//! Assemble a 3D volume from identical depth slices.
[[nodiscard]] auto AssembleVolumeForTest(std::span<const std::byte> bytes,
  uint16_t depth) -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (depth == 0) {
    return oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  std::vector<ScratchImage> slices;
  slices.reserve(depth);

  for (uint16_t slice = 0; slice < depth; ++slice) {
    auto decoded = DecodeToScratchImage(bytes);
    if (!decoded) {
      return oxygen::Err(decoded.error());
    }
    slices.push_back(std::move(*decoded));
  }

  const auto& meta = slices.front().Meta();
  const auto format_info = graphics_detail::GetFormatInfo(meta.format);
  if (format_info.block_size != 1) {
    return oxygen::Err(TextureImportError::kUnsupportedFormat);
  }
  const auto bytes_per_pixel = format_info.bytes_per_block;
  if (bytes_per_pixel == 0) {
    return oxygen::Err(TextureImportError::kUnsupportedFormat);
  }

  ScratchImageMeta volume_meta {
    .texture_type = TextureType::kTexture3D,
    .width = meta.width,
    .height = meta.height,
    .depth = depth,
    .array_layers = 1,
    .mip_levels = 1,
    .format = meta.format,
  };

  ScratchImage volume = ScratchImage::Create(volume_meta);
  if (!volume.IsValid()) {
    return oxygen::Err(TextureImportError::kOutOfMemory);
  }

  auto dst_pixels = volume.GetMutablePixels(0, 0);
  const auto slice_size_bytes = static_cast<std::size_t>(meta.width)
    * static_cast<std::size_t>(meta.height) * bytes_per_pixel;

  if (dst_pixels.size() < slice_size_bytes * depth) {
    return oxygen::Err(TextureImportError::kOutOfMemory);
  }

  for (uint16_t slice = 0; slice < depth; ++slice) {
    const auto src_view = slices.at(slice).GetImage(0, 0);
    std::ranges::copy(
      src_view.pixels, dst_pixels.data() + (slice_size_bytes * slice));
  }

  return oxygen::Ok(std::move(volume));
}

auto MakeSourceBytes(std::vector<std::byte> bytes)
  -> TexturePipeline::SourceBytes
{
  auto owner = std::make_shared<std::vector<std::byte>>(std::move(bytes));
  const std::span<const std::byte> span(owner->data(), owner->size());
  return TexturePipeline::SourceBytes {
    .bytes = span,
    .owner = std::move(owner),
  };
}

auto MakeWorkItem(TextureImportDesc desc, std::string texture_id,
  TexturePipeline::SourceContent source) -> TexturePipeline::WorkItem
{
  return TexturePipeline::WorkItem {
    .source_id = desc.source_id,
    .texture_id = std::move(texture_id),
    .source_key = nullptr,
    .desc = std::move(desc),
    .packing_policy_id = std::string(TightPackedPolicy::Instance().Id()),
    .output_format_policy = TexturePipeline::OutputFormatPolicy::kExplicit,
    .failure_policy = TexturePipeline::FailurePolicy::kStrict,
    .source = std::move(source),
    .stop_token = {},
  };
}

//=== Basic Parity Tests
//===-----------------------------------------------------//

class TexturePipelineNonRegTest : public testing::Test {
protected:
  ImportEventLoop loop_;
  ThreadPool pool_ { loop_, 2 };
};

NOLINT_TEST_F(TexturePipelineNonRegTest, CollectParityWithSyncCookerMatches)
{
  TextureImportDesc desc;
  desc.source_id = "parity.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  const auto bytes = GetTestImageBytes();
  const auto sync = CookTexture(bytes, desc, TightPackedPolicy::Instance());
  ASSERT_HAS_VALUE(sync);

  auto source_bytes
    = MakeSourceBytes(std::vector<std::byte>(bytes.begin(), bytes.end()));

  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem(desc, "parity.bmp", std::move(source_bytes)), pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_TRUE(result.success);
  ASSERT_HAS_VALUE(result.cooked);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.cooked->payload, sync->payload);
  EXPECT_EQ(result.cooked->desc.width, sync->desc.width);
  EXPECT_EQ(result.cooked->desc.height, sync->desc.height);
  EXPECT_EQ(result.cooked->desc.format, sync->desc.format);
  EXPECT_EQ(result.cooked->desc.mip_levels, sync->desc.mip_levels);
  EXPECT_EQ(result.cooked->desc.content_hash, sync->desc.content_hash);
}

NOLINT_TEST_F(TexturePipelineNonRegTest, CollectDepthSlicesParityMatches)
{
  TextureImportDesc desc;
  desc.source_id = "volume.bmp";
  desc.texture_type = TextureType::kTexture3D;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  constexpr uint16_t kDepth = 2;
  const auto bytes = GetTestImageBytes();
  auto assembled = AssembleVolumeForTest(bytes, kDepth);
  ASSERT_HAS_VALUE(assembled);

  auto expected
    = CookTexture(std::move(*assembled), desc, TightPackedPolicy::Instance());
  ASSERT_HAS_VALUE(expected);

  TextureSourceSet sources;
  for (uint16_t slice = 0; slice < kDepth; ++slice) {
    sources.AddDepthSlice(
      slice, std::vector<std::byte>(bytes.begin(), bytes.end()), "slice.bmp");
  }

  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem(desc, "volume.bmp", std::move(sources)), pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_TRUE(result.success);
  ASSERT_HAS_VALUE(result.cooked);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.cooked->payload, expected->payload);
  EXPECT_EQ(result.cooked->desc.depth, kDepth);
  EXPECT_EQ(result.cooked->desc.texture_type, TextureType::kTexture3D);
}

NOLINT_TEST_F(
  TexturePipelineNonRegTest, CollectDepthSlicesWithGapEmitsDiagnostic)
{
  TextureImportDesc desc;
  desc.source_id = "volume_gap.bmp";
  desc.texture_type = TextureType::kTexture3D;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  const auto bytes = GetTestImageBytes();
  TextureSourceSet sources;
  sources.AddDepthSlice(
    0, std::vector<std::byte>(bytes.begin(), bytes.end()), "slice0.bmp");
  sources.AddDepthSlice(
    2, std::vector<std::byte>(bytes.begin(), bytes.end()), "slice2.bmp");

  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem(desc, "volume_gap.bmp", std::move(sources)), pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics.at(0).code, "texture.cook_failed");
}

} // namespace
