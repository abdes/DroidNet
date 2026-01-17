//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/OxCo/Run.h>
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

//! Creates a minimal valid BMP image (2x2, 32-bit BGRA).
/*!
 @return A byte vector containing a valid BMP file with 4 colored pixels.
*/
[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
  // BMP file header (14 bytes) + DIB header (40 bytes) + 4 pixels (16 bytes)
  constexpr uint32_t kFileSize = 14u + 40u + 16u;
  constexpr uint32_t kPixelOffset = 54u;
  constexpr uint32_t kDibHeaderSize = 40u;
  constexpr int32_t kWidth = 2;
  constexpr int32_t kHeight = 2;
  constexpr uint16_t kPlanes = 1u;
  constexpr uint16_t kBitsPerPixel = 32u;

  std::vector<std::byte> bytes;
  bytes.reserve(kFileSize);

  const auto push_u16 = [&bytes](uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
  };
  const auto push_u32 = [&bytes](uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  const auto push_i32 = [&bytes](int32_t value) {
    const auto unsigned_val = static_cast<uint32_t>(value);
    bytes.push_back(static_cast<std::byte>(unsigned_val & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 24u) & 0xFFu));
  };
  const auto push_bgra
    = [&bytes](uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) {
        bytes.push_back(static_cast<std::byte>(blue));
        bytes.push_back(static_cast<std::byte>(green));
        bytes.push_back(static_cast<std::byte>(red));
        bytes.push_back(static_cast<std::byte>(alpha));
      };

  // BMP file header (14 bytes)
  bytes.push_back(static_cast<std::byte>('B'));
  bytes.push_back(static_cast<std::byte>('M'));
  push_u32(kFileSize);
  push_u16(0u);
  push_u16(0u);
  push_u32(kPixelOffset);

  // DIB header (BITMAPINFOHEADER, 40 bytes)
  push_u32(kDibHeaderSize);
  push_i32(kWidth);
  push_i32(kHeight);
  push_u16(kPlanes);
  push_u16(kBitsPerPixel);
  push_u32(0u);
  push_u32(16u);
  push_i32(2835);
  push_i32(2835);
  push_u32(0u);
  push_u32(0u);

  // Pixel data (bottom-up, BGRA format)
  push_bgra(0u, 0u, 255u, 255u);
  push_bgra(255u, 255u, 255u, 255u);
  push_bgra(255u, 0u, 0u, 255u);
  push_bgra(0u, 255u, 0u, 255u);

  return bytes;
}

//! Returns the test BMP image as a span of bytes.
[[nodiscard]] auto GetTestImageBytes() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp2x2();
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
    .output_format_is_override = true,
    .failure_policy = TexturePipeline::FailurePolicy::kStrict,
    .source = std::move(source),
    .stop_token = {},
  };
}

auto RunPipelineOnce(ImportEventLoop& loop, TexturePipeline::WorkItem item)
  -> TexturePipeline::WorkResult
{
  TexturePipeline::WorkResult result;
  co::ThreadPool pool(loop, 2);

  co::Run(loop, [&]() -> co::Co<> {
    TexturePipeline pipeline(pool,
      TexturePipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  return result;
}

//=== Edge Case Tests
//===-----------------------------------------------------//

class TexturePipelineEdgeTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Empty byte payloads should fail with a cook diagnostic.
NOLINT_TEST_F(TexturePipelineEdgeTest, Collect_EmptySourceBytes_Fails)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "empty_bytes.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  auto source_bytes = MakeSourceBytes({});
  auto item = MakeWorkItem(desc, "empty_bytes.bmp", std::move(source_bytes),
    std::string(TightPackedPolicy::Instance().Id()));

  // Act
  const auto result = RunPipelineOnce(loop_, std::move(item));

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Empty source sets should fail with a cook diagnostic.
NOLINT_TEST_F(TexturePipelineEdgeTest, Collect_EmptySourceSet_Fails)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "empty_set.bmp";
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.mip_policy = MipPolicy::kNone;

  TextureSourceSet sources;
  auto item = MakeWorkItem(desc, "empty_set.bmp", std::move(sources),
    std::string(TightPackedPolicy::Instance().Id()));

  // Act
  const auto result = RunPipelineOnce(loop_, std::move(item));

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Nonzero depth slices with a non-3D target should fail.
NOLINT_TEST_F(TexturePipelineEdgeTest, Collect_DepthSliceNon3D_Fails)
{
  // Arrange
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

  // Act
  const auto result = RunPipelineOnce(loop_, std::move(item));

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Duplicate array layers should fail assembly.
NOLINT_TEST_F(TexturePipelineEdgeTest, Collect_DuplicateArrayLayer_Fails)
{
  // Arrange
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

  // Act
  const auto result = RunPipelineOnce(loop_, std::move(item));

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Unknown packing policy should emit a warning but still succeed.
NOLINT_TEST_F(TexturePipelineEdgeTest, Collect_UnknownPackingPolicy_Warns)
{
  // Arrange
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

  // Act
  const auto result = RunPipelineOnce(loop_, std::move(item));

  // Assert
  EXPECT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  EXPECT_FALSE(result.used_placeholder);
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.packing_policy_unknown");
}

} // namespace
