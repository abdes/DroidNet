//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/TexturePipeline.cpp,
//   Import/TextureSourceAssembly.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TextureImportPresets.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Test/Support/PipelineHarness.h>
#include <Oxygen/Cooker/Test/Support/TestImages.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
using oxygen::Format;
using oxygen::TextureType;

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
  TexturePipeline::SourceContent source, std::string packing_policy_id)
  -> TexturePipeline::WorkItem
{
  return TexturePipeline::WorkItem {
    .source_id = desc.source_id,
    .texture_id = std::move(texture_id),
    .source_key = nullptr,
    .desc = std::move(desc),
    .packing_policy_id = std::move(packing_policy_id),
    .output_format_policy = TexturePipeline::OutputFormatPolicy::kExplicit,
    .failure_policy = TexturePipeline::FailurePolicy::kStrict,
    .source = std::move(source),
    .stop_token = {},
  };
}

//=== Edge Case Tests
//===-----------------------------------------------------//

class TexturePipelineEdgeTest : public testing::Test {
protected:
  ImportEventLoop loop_;
  ThreadPool pool_ { loop_, 2 };

  auto RunOnce(TexturePipeline::WorkItem item) -> TexturePipeline::WorkResult
  {
    return oxygen::cooker::test::RunPipelineOnce<TexturePipeline>(loop_,
      std::move(item), pool_,
      TexturePipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });
  }
};

NOLINT_TEST_F(
  TexturePipelineEdgeTest, MaterialPresetsKeepCompressionAndMipChains)
{
  for (const auto preset : { TexturePreset::kAlbedo, TexturePreset::kNormal }) {
    const auto desc = MakeDescFromPreset(preset);
    auto item
      = MakeWorkItem(desc, "material.bmp", MakeSourceBytes(MakeBmp(2, 2)),
        std::string(TightPackedPolicy::Instance().Id()));
    item.output_format_policy
      = TexturePipeline::OutputFormatPolicy::kMaterialPreset;

    const auto result = RunOnce(std::move(item));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.cooked);
    EXPECT_EQ(result.cooked->desc.format, desc.output_format);
    EXPECT_EQ(result.cooked->desc.mip_levels, 2U);
  }
}

NOLINT_TEST_F(
  TexturePipelineEdgeTest, SourceAndExplicitPoliciesRetainTheirStorageIntent)
{
  for (const auto policy :
    { TexturePipeline::OutputFormatPolicy::kPreserveSource,
      TexturePipeline::OutputFormatPolicy::kExplicit }) {
    auto desc = MakeDescFromPreset(TexturePreset::kAlbedo);
    if (policy == TexturePipeline::OutputFormatPolicy::kExplicit) {
      desc.output_format = Format::kRGBA8UNorm;
      desc.bc7_quality = Bc7Quality::kNone;
    }
    auto item
      = MakeWorkItem(desc, "storage.bmp", MakeSourceBytes(MakeBmp(2, 2)),
        std::string(TightPackedPolicy::Instance().Id()));
    item.output_format_policy = policy;
    const auto result = RunOnce(std::move(item));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.cooked);
    EXPECT_EQ(result.cooked->desc.format, Format::kRGBA8UNorm);
    EXPECT_EQ(result.cooked->desc.mip_levels, 2U);
  }
}

NOLINT_TEST_F(TexturePipelineEdgeTest, MaterialPresetPreservesHdrRadiance)
{
  auto image = ScratchImage::Create(
    { .width = 1U, .height = 1U, .format = Format::kRGBA32Float });
  const auto pixel = std::array { 64.0F, 8.0F, 2.0F, 1.0F };
  std::memcpy(image.GetMutablePixels(0, 0).data(), pixel.data(), sizeof(pixel));
  auto desc = MakeDescFromPreset(TexturePreset::kEmissive);
  desc.source_color_space = oxygen::ColorSpace::kLinear;
  auto item = MakeWorkItem(desc, "emissive.exr", std::move(image),
    std::string(TightPackedPolicy::Instance().Id()));
  item.output_format_policy
    = TexturePipeline::OutputFormatPolicy::kMaterialPreset;
  const auto result = RunOnce(std::move(item));
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked);
  EXPECT_EQ(result.cooked->desc.format, Format::kRGBA32Float);
  ASSERT_FALSE(result.cooked->layouts.empty());
  oxygen::data::pak::render::TexturePayloadHeader header {};
  ASSERT_GE(result.cooked->payload.size(), sizeof(header));
  std::memcpy(&header, result.cooked->payload.data(), sizeof(header));
  const auto& layout = result.cooked->layouts.front();
  std::array<float, 4> actual {};
  const auto offset = header.data_offset_bytes + layout.offset_bytes;
  ASSERT_GE(result.cooked->payload.size(), offset + sizeof(actual));
  std::memcpy(
    actual.data(), result.cooked->payload.data() + offset, sizeof(actual));
  EXPECT_EQ(actual, pixel);
}

//! Empty byte payloads should fail with a cook diagnostic.
NOLINT_TEST_F(TexturePipelineEdgeTest, CollectEmptySourceBytesFails)
{
  TextureImportDesc desc;
  desc.source_id = "empty_bytes.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  auto source_bytes = MakeSourceBytes({});
  auto item = MakeWorkItem(desc, "empty_bytes.bmp", std::move(source_bytes),
    std::string(TightPackedPolicy::Instance().Id()));

  const auto result = RunOnce(std::move(item));

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Empty source sets should fail with a cook diagnostic.
NOLINT_TEST_F(TexturePipelineEdgeTest, CollectEmptySourceSetFails)
{
  TextureImportDesc desc;
  desc.source_id = "empty_set.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  TextureSourceSet sources;
  auto item = MakeWorkItem(desc, "empty_set.bmp", std::move(sources),
    std::string(TightPackedPolicy::Instance().Id()));

  const auto result = RunOnce(std::move(item));

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Nonzero depth slices with a non-3D target should fail.
NOLINT_TEST_F(TexturePipelineEdgeTest, CollectDepthSliceNon3DFails)
{
  TextureImportDesc desc;
  desc.source_id = "slice_non3d.bmp";
  desc.texture_type = TextureType::kTexture2D;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  const auto bytes = GetTestImageBytes();
  TextureSourceSet sources;
  sources.AddDepthSlice(
    1, std::vector<std::byte>(bytes.begin(), bytes.end()), "slice1.bmp");

  auto item = MakeWorkItem(desc, "slice_non3d.bmp", std::move(sources),
    std::string(TightPackedPolicy::Instance().Id()));

  const auto result = RunOnce(std::move(item));

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Duplicate array layers should fail assembly.
NOLINT_TEST_F(TexturePipelineEdgeTest, CollectDuplicateArrayLayerFails)
{
  TextureImportDesc desc;
  desc.source_id = "dup_layer.bmp";
  desc.texture_type = TextureType::kTexture2DArray;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  const auto bytes = GetTestImageBytes();
  TextureSourceSet sources;
  sources.AddArrayLayer(
    0, std::vector<std::byte>(bytes.begin(), bytes.end()), "layer0_a.bmp");
  sources.AddArrayLayer(
    0, std::vector<std::byte>(bytes.begin(), bytes.end()), "layer0_b.bmp");

  auto item = MakeWorkItem(desc, "dup_layer.bmp", std::move(sources),
    std::string(TightPackedPolicy::Instance().Id()));

  const auto result = RunOnce(std::move(item));

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Unknown packing policy should emit a warning but still succeed.
NOLINT_TEST_F(TexturePipelineEdgeTest, CollectUnknownPackingPolicyWarns)
{
  TextureImportDesc desc;
  desc.source_id = "unknown_policy.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  const auto bytes = GetTestImageBytes();
  auto source_bytes
    = MakeSourceBytes(std::vector<std::byte>(bytes.begin(), bytes.end()));

  auto item = MakeWorkItem(
    desc, "unknown_policy.bmp", std::move(source_bytes), "unknown-policy");

  const auto result = RunOnce(std::move(item));

  EXPECT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  EXPECT_FALSE(result.used_placeholder);
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.packing_policy_unknown");
}

} // namespace
