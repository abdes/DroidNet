//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/TextureSourceAssembly.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::AssembleCubeFromFaces;
using oxygen::content::import::ConvertEquirectangularToCube;
using oxygen::content::import::CubeFace;
using oxygen::content::import::CubeMapImageLayout;
using oxygen::content::import::EquirectToCubeOptions;
using oxygen::content::import::ExtractCubeFacesFromLayout;
using oxygen::content::import::kCubeFaceCount;
using oxygen::content::import::MipFilter;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::SubresourceId;
using oxygen::content::import::TextureImportError;
using oxygen::content::import::TextureSource;
using oxygen::content::import::TextureSourceSet;
using oxygen::content::import::to_string;

namespace {

//=== CubeFace Tests
//===----------------------------------------------------===//

//! to_string returns the expected string for all cube face values.
NOLINT_TEST(TextureSourceAssemblyTest, ToStringReturnsExpectedStrings)
{
  EXPECT_STREQ(to_string(CubeFace::kPositiveX), "PositiveX");
  EXPECT_STREQ(to_string(CubeFace::kNegativeX), "NegativeX");
  EXPECT_STREQ(to_string(CubeFace::kPositiveY), "PositiveY");
  EXPECT_STREQ(to_string(CubeFace::kNegativeY), "NegativeY");
  EXPECT_STREQ(to_string(CubeFace::kPositiveZ), "PositiveZ");
  EXPECT_STREQ(to_string(CubeFace::kNegativeZ), "NegativeZ");
}

//! CubeFace enum values match D3D12/Vulkan face ordering (0-5).
NOLINT_TEST(TextureSourceAssemblyTest, EnumValuesMatchExpectedOrdering)
{
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveX), 0);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeX), 1);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveY), 2);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeY), 3);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveZ), 4);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeZ), 5);
}

//! kCubeFaceCount constant equals 6.
NOLINT_TEST(TextureSourceAssemblyTest, CubeFaceCountIsSix)
{
  EXPECT_EQ(kCubeFaceCount, 6);
}

//=== SubresourceId Tests
//===-----------------------------------------------===//

//! Default-constructed SubresourceId has all fields zero.
NOLINT_TEST(TextureSourceAssemblyTest, DefaultConstructionAllFieldsZero)
{
  const SubresourceId id {};

  EXPECT_EQ(id.array_layer, 0);
  EXPECT_EQ(id.mip_level, 0);
  EXPECT_EQ(id.depth_slice, 0);
}

//! SubresourceId equality comparison works correctly.
NOLINT_TEST(TextureSourceAssemblyTest, EqualityComparesAllFields)
{
  const SubresourceId id1 {
    .array_layer = 1,
    .mip_level = 2,
    .depth_slice = 3,
  };
  const SubresourceId id2 {
    .array_layer = 1,
    .mip_level = 2,
    .depth_slice = 3,
  };
  const SubresourceId id3 {
    .array_layer = 0,
    .mip_level = 2,
    .depth_slice = 3,
  };

  EXPECT_EQ(id1, id2);
  EXPECT_NE(id1, id3);
}

//=== TextureSourceSet Basic Tests
//===--------------------------------------===//

//! Default-constructed TextureSourceSet is empty.
NOLINT_TEST(TextureSourceAssemblyTest, DefaultConstructionIsEmpty)
{
  const TextureSourceSet set;

  EXPECT_TRUE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 0);
  EXPECT_TRUE(set.Sources().empty());
}

//! Add() adds a source to the set.
NOLINT_TEST(TextureSourceAssemblyTest, AddAddsSource)
{
  TextureSourceSet set;
  std::vector<std::byte> bytes { std::byte { 0x01 }, std::byte { 0x02 } };

  set.Add(TextureSource {
    .bytes = bytes,
    .subresource = SubresourceId { .array_layer = 1, .mip_level = 2 },
    .source_id = "test.png",
  });

  EXPECT_FALSE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 1);
}

//! Clear() removes all sources.
NOLINT_TEST(TextureSourceAssemblyTest, ClearRemovesAllSources)
{
  TextureSourceSet set;
  set.Add(TextureSource {
    .bytes = { std::byte { 0x01 } },
    .subresource = {},
    .source_id = "test.png",
  });
  ASSERT_FALSE(set.IsEmpty());

  set.Clear();

  EXPECT_TRUE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 0);
}

//! GetSource() returns the source at the given index.
NOLINT_TEST(TextureSourceAssemblyTest, GetSourceReturnsCorrectSource)
{
  TextureSourceSet set;
  set.Add(TextureSource {
    .bytes = { std::byte { 0xAA } },
    .subresource = SubresourceId { .array_layer = 5 },
    .source_id = "first.png",
  });
  set.Add(TextureSource {
    .bytes = { std::byte { 0xBB } },
    .subresource = SubresourceId { .array_layer = 7 },
    .source_id = "second.png",
  });

  const auto& source0 = set.GetSource(0);
  const auto& source1 = set.GetSource(1);

  EXPECT_EQ(source0.source_id, "first.png");
  EXPECT_EQ(source0.subresource.array_layer, 5);
  EXPECT_EQ(source1.source_id, "second.png");
  EXPECT_EQ(source1.subresource.array_layer, 7);
}

//! GetSource() throws when index is out of range.
NOLINT_TEST(TextureSourceAssemblyTest, GetSourceThrowsOnOutOfRange)
{
  const TextureSourceSet set;

  EXPECT_THROW((void)set.GetSource(0), std::out_of_range);
}

//=== TextureSourceSet Array Layer Tests
//===---------------------------------===//

//! AddArrayLayer() sets correct subresource fields.
NOLINT_TEST(TextureSourceAssemblyTest, AddArrayLayerSetsCorrectFields)
{
  TextureSourceSet set;

  set.AddArrayLayer(3, { std::byte { 0x01 } }, "layer3.png");

  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 3);
  EXPECT_EQ(source.subresource.mip_level, 0);
  EXPECT_EQ(source.subresource.depth_slice, 0);
  EXPECT_EQ(source.source_id, "layer3.png");
}

//! Multiple AddArrayLayer() calls create separate sources.
NOLINT_TEST(TextureSourceAssemblyTest, AddArrayLayerMultipleLayers)
{
  TextureSourceSet set;

  set.AddArrayLayer(0, { std::byte { 0x00 } }, "layer0.png");
  set.AddArrayLayer(1, { std::byte { 0x01 } }, "layer1.png");
  set.AddArrayLayer(2, { std::byte { 0x02 } }, "layer2.png");

  ASSERT_EQ(set.Count(), 3);
  EXPECT_EQ(set.GetSource(0).subresource.array_layer, 0);
  EXPECT_EQ(set.GetSource(1).subresource.array_layer, 1);
  EXPECT_EQ(set.GetSource(2).subresource.array_layer, 2);
}

//=== TextureSourceSet Cube Face Tests
//===----------------------------------===//

//! AddCubeFace() maps face to correct array layer index.
NOLINT_TEST(TextureSourceAssemblyTest, AddCubeFaceMapsToArrayLayer)
{
  TextureSourceSet set;

  set.AddCubeFace(CubeFace::kPositiveX, { std::byte { 0x00 } }, "px.hdr");
  set.AddCubeFace(CubeFace::kNegativeZ, { std::byte { 0x05 } }, "nz.hdr");

  EXPECT_EQ(set.GetSource(0).subresource.array_layer, 0); // kPositiveX = 0
  EXPECT_EQ(set.GetSource(1).subresource.array_layer, 5); // kNegativeZ = 5
}

//! AddCubeFace() for all 6 faces creates correct mapping.
NOLINT_TEST(TextureSourceAssemblyTest, AddAllFacesCreatesCompleteCube)
{
  TextureSourceSet set;

  set.AddCubeFace(CubeFace::kPositiveX, { std::byte { 0x00 } }, "px.hdr");
  set.AddCubeFace(CubeFace::kNegativeX, { std::byte { 0x01 } }, "nx.hdr");
  set.AddCubeFace(CubeFace::kPositiveY, { std::byte { 0x02 } }, "py.hdr");
  set.AddCubeFace(CubeFace::kNegativeY, { std::byte { 0x03 } }, "ny.hdr");
  set.AddCubeFace(CubeFace::kPositiveZ, { std::byte { 0x04 } }, "pz.hdr");
  set.AddCubeFace(CubeFace::kNegativeZ, { std::byte { 0x05 } }, "nz.hdr");

  ASSERT_EQ(set.Count(), 6);
  for (size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.array_layer, i);
  }
}

//=== TextureSourceSet Depth Slice Tests
//===---------------------------------===//

//! AddDepthSlice() sets correct subresource fields.
NOLINT_TEST(TextureSourceAssemblyTest, AddDepthSliceSetsCorrectFields)
{
  TextureSourceSet set;

  set.AddDepthSlice(7, { std::byte { 0x07 } }, "slice7.png");

  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 0);
  EXPECT_EQ(source.subresource.mip_level, 0);
  EXPECT_EQ(source.subresource.depth_slice, 7);
  EXPECT_EQ(source.source_id, "slice7.png");
}

//! Multiple AddDepthSlice() calls create separate sources.
NOLINT_TEST(TextureSourceAssemblyTest, AddDepthSliceMultipleSlices)
{
  TextureSourceSet set;

  for (uint16_t i = 0; i < 16; ++i) {
    set.AddDepthSlice(
      i, { static_cast<std::byte>(i) }, "slice" + std::to_string(i) + ".png");
  }

  ASSERT_EQ(set.Count(), 16);
  for (size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.depth_slice, i);
  }
}

//=== TextureSourceSet Mip Level Tests
//===----------------------------------===//

//! AddMipLevel() sets correct subresource fields.
NOLINT_TEST(TextureSourceAssemblyTest, AddMipLevelSetsCorrectFields)
{
  TextureSourceSet set;

  set.AddMipLevel(2, 3, { std::byte { 0x23 } }, "layer2_mip3.png");

  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 2);
  EXPECT_EQ(source.subresource.mip_level, 3);
  EXPECT_EQ(source.subresource.depth_slice, 0);
  EXPECT_EQ(source.source_id, "layer2_mip3.png");
}

//! AddMipLevel() supports pre-authored mip chains.
NOLINT_TEST(TextureSourceAssemblyTest, AddMipLevelFullMipChain)
{
  TextureSourceSet set;

  // Add mip chain for a single array layer
  set.AddMipLevel(0, 0, { std::byte { 0x00 } }, "mip0.png");
  set.AddMipLevel(0, 1, { std::byte { 0x01 } }, "mip1.png");
  set.AddMipLevel(0, 2, { std::byte { 0x02 } }, "mip2.png");
  set.AddMipLevel(0, 3, { std::byte { 0x03 } }, "mip3.png");

  ASSERT_EQ(set.Count(), 4);
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.array_layer, 0);
    EXPECT_EQ(set.GetSource(i).subresource.mip_level, i);
  }
}

//=== TextureSourceSet Sources() Tests
//===----------------------------------===//

//! Sources() returns a span over all added sources.
NOLINT_TEST(TextureSourceAssemblyTest, SourcesReturnsAllSources)
{
  TextureSourceSet set;
  set.AddArrayLayer(0, { std::byte { 0x00 } }, "a.png");
  set.AddArrayLayer(1, { std::byte { 0x01 } }, "b.png");
  set.AddArrayLayer(2, { std::byte { 0x02 } }, "c.png");

  const auto sources = set.Sources();

  ASSERT_EQ(sources.size(), 3U);
  EXPECT_EQ(sources[0].source_id, "a.png");
  EXPECT_EQ(sources[1].source_id, "b.png");
  EXPECT_EQ(sources[2].source_id, "c.png");
}

//=== EquirectToCubeOptions Tests
//===---------------------------------------===//

//! Default EquirectToCubeOptions has expected values.
NOLINT_TEST(TextureSourceAssemblyTest, DefaultsToFaceSize512AndKaiserFilter)
{
  const EquirectToCubeOptions options {};

  EXPECT_EQ(options.face_size, 512);
  EXPECT_EQ(options.sample_filter, MipFilter::kKaiser);
}

//=== AssembleCubeFromFaces Tests
//===-----------------------------------------===//

//! Create a simple 2x2 RGBA8 test face with a unique color.
auto MakeTestFace(uint8_t color_value) -> ScratchImage
{
  constexpr uint32_t kWidth = 2;
  constexpr uint32_t kHeight = 2;
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
  std::vector<std::byte> data(
    static_cast<size_t>(kWidth) * kHeight * kBytesPerPixel,
    std::byte { color_value });
  return ScratchImage::CreateFromData(
    kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(data));
}

//! AssembleCubeFromFaces with valid faces creates a cube map.
NOLINT_TEST(TextureSourceAssemblyTest, ValidFacesCreatesCubeMap)
{
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x00),
    MakeTestFace(0x11),
    MakeTestFace(0x22),
    MakeTestFace(0x33),
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  auto result = AssembleCubeFromFaces(faces);

  ASSERT_HAS_VALUE(result);
  EXPECT_TRUE(result->IsValid());
  const auto& meta = result->Meta();
  EXPECT_EQ(meta.width, 2);
  EXPECT_EQ(meta.height, 2);
  EXPECT_EQ(meta.array_layers, 6);
  EXPECT_EQ(meta.texture_type, TextureType::kTextureCube);
}

//! Stored +X takes authored -X and the reverse; the other faces keep their
//! layer (`space::environment`: authored faces are stored reflected across
//! cube-space x = 0).
NOLINT_TEST(TextureSourceAssemblyTest, StoresAuthoredXFacesSwapped)
{
  // Each face has unique pixel data
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x10),
    MakeTestFace(0x20),
    MakeTestFace(0x30),
    MakeTestFace(0x40),
    MakeTestFace(0x50),
    MakeTestFace(0x60),
  };

  auto result = AssembleCubeFromFaces(faces);
  ASSERT_HAS_VALUE(result);

  constexpr std::array<uint8_t, kCubeFaceCount> kExpected {
    0x20,
    0x10,
    0x30,
    0x40,
    0x50,
    0x60,
  };
  for (uint16_t i = 0; i < kCubeFaceCount; ++i) {
    const auto image = result->GetImage(i, 0);
    EXPECT_EQ(static_cast<uint8_t>(image.pixels[0]), kExpected.at(i));
  }
}

//! The byte values of a test face's left and right columns.
struct TwoColumns final {
  uint8_t left { 0U };
  uint8_t right { 0U };
};

//! A 2x2 RGBA8 face whose columns hold `columns`.
auto MakeTwoColumnFace(const TwoColumns columns) -> ScratchImage
{
  constexpr uint32_t kSize = 2;
  constexpr uint32_t kBytesPerPixel = 4;
  std::vector<std::byte> data;
  for (uint32_t y = 0; y < kSize; ++y) {
    data.insert(data.end(), kBytesPerPixel, std::byte { columns.left });
    data.insert(data.end(), kBytesPerPixel, std::byte { columns.right });
  }
  return ScratchImage::CreateFromData(
    kSize, kSize, Format::kRGBA8UNorm, kSize * kBytesPerPixel, std::move(data));
}

//! A texel of a cube face, rows top first.
struct FaceTexel final {
  uint16_t face { 0U };
  uint32_t x { 0U };
  uint32_t y { 0U };
};

//! The first byte of an RGBA8 cube texel.
auto FaceByte(const ScratchImage& cube, const FaceTexel texel) -> uint8_t
{
  const auto view = cube.GetImage(texel.face, 0);
  return static_cast<uint8_t>(
    view.pixels[(static_cast<size_t>(texel.y) * view.row_pitch_bytes)
      + (static_cast<size_t>(texel.x) * 4U)]);
}

//! Every stored face is its authored face mirrored about the vertical axis:
//! the authored right column becomes the stored left column.
NOLINT_TEST(TextureSourceAssemblyTest, StoresAuthoredFacesMirrored)
{
  std::array<ScratchImage, 6> faces = {
    MakeTwoColumnFace({ .left = 0x10, .right = 0x11 }),
    MakeTwoColumnFace({ .left = 0x20, .right = 0x21 }),
    MakeTwoColumnFace({ .left = 0x30, .right = 0x31 }),
    MakeTwoColumnFace({ .left = 0x40, .right = 0x41 }),
    MakeTwoColumnFace({ .left = 0x50, .right = 0x51 }),
    MakeTwoColumnFace({ .left = 0x60, .right = 0x61 }),
  };

  auto result = AssembleCubeFromFaces(faces);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(FaceByte(*result, { .face = 4, .x = 0, .y = 1 }), 0x51);
  EXPECT_EQ(FaceByte(*result, { .face = 4, .x = 1, .y = 1 }), 0x50);
  EXPECT_EQ(FaceByte(*result, { .face = 0, .x = 0, .y = 0 }), 0x21);
  EXPECT_EQ(FaceByte(*result, { .face = 1, .x = 1, .y = 0 }), 0x10);
}

//! A layout image stores its faces like `AssembleCubeFromFaces`: X faces
//! swapped, every face mirrored. The horizontal strip holds authored faces
//! +X, -X, +Y, -Y, +Z, -Z from left to right.
NOLINT_TEST(TextureSourceAssemblyTest, StoresLayoutFacesReflected)
{
  constexpr uint32_t kFace = 2;
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kWidth = kFace * kCubeFaceCount;
  std::vector<std::byte> data;
  for (uint32_t y = 0; y < kFace; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const auto face = x / kFace;
      const auto value
        = static_cast<uint8_t>(((face + 1U) * 0x10U) + (x % kFace));
      data.insert(data.end(), kBytesPerPixel, std::byte { value });
    }
  }
  const auto strip = ScratchImage::CreateFromData(kWidth, kFace,
    Format::kRGBA8UNorm, kWidth * kBytesPerPixel, std::move(data));

  auto result
    = ExtractCubeFacesFromLayout(strip, CubeMapImageLayout::kHorizontalStrip);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(FaceByte(*result, { .face = 0, .x = 0, .y = 0 }), 0x21);
  EXPECT_EQ(FaceByte(*result, { .face = 1, .x = 0, .y = 0 }), 0x11);
  EXPECT_EQ(FaceByte(*result, { .face = 4, .x = 0, .y = 1 }), 0x51);
  EXPECT_EQ(FaceByte(*result, { .face = 4, .x = 1, .y = 1 }), 0x50);
}

//! AssembleCubeFromFaces fails with invalid face.
NOLINT_TEST(TextureSourceAssemblyTest, InvalidFaceFails)
{
  // One invalid face
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x00),
    MakeTestFace(0x11),
    MakeTestFace(0x22),
    ScratchImage {}, // Invalid face
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  auto result = AssembleCubeFromFaces(faces);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! AssembleCubeFromFaces fails with non-square face.
NOLINT_TEST(TextureSourceAssemblyTest, NonSquareFaceFails)
{
  // Create a non-square face (4x2)
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 2; // Non-square
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
  std::vector<std::byte> data(
    static_cast<size_t>(kWidth) * kHeight * kBytesPerPixel);
  auto non_square = ScratchImage::CreateFromData(
    kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(data));

  std::array<ScratchImage, 6> faces = {
    std::move(non_square),
    MakeTestFace(0x11),
    MakeTestFace(0x22),
    MakeTestFace(0x33),
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  auto result = AssembleCubeFromFaces(faces);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! AssembleCubeFromFaces fails with mismatched dimensions.
NOLINT_TEST(TextureSourceAssemblyTest, MismatchedDimensionsFails)
{
  // Create a larger face (4x4 vs 2x2)
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 4;
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
  std::vector<std::byte> large_data(
    static_cast<size_t>(kWidth) * kHeight * kBytesPerPixel);
  auto large_face = ScratchImage::CreateFromData(
    kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(large_data));

  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x00),
    std::move(large_face),
    MakeTestFace(0x22),
    MakeTestFace(0x33),
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  auto result = AssembleCubeFromFaces(faces);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDimensionMismatch);
}

//=== ConvertEquirectangularToCube Tests
//===---------------------------------===//

//! Pixel dimensions of a test image.
struct ImageSize {
  uint32_t width;
  uint32_t height;
};

//! RGBA color of a solid test image.
struct Rgba {
  float r = 1.0F;
  float g = 0.5F;
  float b = 0.0F;
  float a = 1.0F;
};

//! Copy float values into a freshly allocated byte buffer.
auto FloatsToBytes(const std::span<const float> values)
  -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(values.size_bytes());
  std::memcpy(bytes.data(), values.data(), values.size_bytes());
  return bytes;
}

//! Read the float at `float_index` of a byte buffer.
auto ReadFloat(const std::span<const std::byte> bytes, const size_t float_index)
  -> float
{
  float value = 0.0F;
  std::memcpy(
    &value, bytes.subspan(float_index * sizeof(value)).data(), sizeof(value));
  return value;
}

//! Create a test equirectangular image (2:1 aspect, RGBA32F).
auto MakeEquirect(const ImageSize size, const Rgba& color = {}) -> ScratchImage
{
  const size_t pixel_count = static_cast<size_t>(size.width) * size.height;
  std::vector<float> floats(pixel_count * 4);
  for (size_t i = 0; i < pixel_count; ++i) {
    floats[(i * 4) + 0] = color.r;
    floats[(i * 4) + 1] = color.g;
    floats[(i * 4) + 2] = color.b;
    floats[(i * 4) + 3] = color.a;
  }
  return ScratchImage::CreateFromData(size.width, size.height,
    Format::kRGBA32Float, size.width * 16, FloatsToBytes(floats));
}

//! Create a test equirectangular with gradient (longitude varies R, lat G).
auto MakeGradientEquirect(const uint32_t width, const uint32_t height)
  -> ScratchImage
{
  std::vector<float> floats(static_cast<size_t>(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t idx = ((static_cast<size_t>(y) * width) + x) * 4;
      // R = horizontal position (longitude)
      floats[idx + 0] = static_cast<float>(x) / static_cast<float>(width - 1);
      // G = vertical position (latitude)
      floats[idx + 1] = static_cast<float>(y) / static_cast<float>(height - 1);
      floats[idx + 2] = 0.0F;
      floats[idx + 3] = 1.0F;
    }
  }
  return ScratchImage::CreateFromData(
    width, height, Format::kRGBA32Float, width * 16, FloatsToBytes(floats));
}

//! Column and row of a texel, rows top first.
struct TexelCoord {
  uint32_t x;
  uint32_t y;
};

//! One channel of the texel at `texel` of a cube face.
auto CubeTexel(const ScratchImage& cube, const uint16_t face,
  const TexelCoord texel, const uint32_t channel) -> float
{
  const auto view = cube.GetImage(face, 0);
  const auto row
    = view.pixels.subspan(static_cast<size_t>(texel.y) * view.row_pitch_bytes);
  return ReadFloat(row, (static_cast<size_t>(texel.x) * 4U) + channel);
}

//! A panorama's centre (u = 0.5) is Oxygen's forward, -Y, which the cube holds
//! on hardware face +Z (index 4); u grows toward world -X
//! (`space::environment`), so world +X (hardware +X, index 0) holds u = 0.25;
//! its top row is Oxygen's up, +Z, on hardware face +Y (index 2). Faces keep
//! their rows top first, as the shaders sample them (CubemapGeometry.hlsli).
NOLINT_TEST(TextureSourceAssemblyTest, EquirectMapsOxygenAxesOntoHardwareFaces)
{
  constexpr uint32_t kFace = 32;
  auto equirect = MakeGradientEquirect(256, 128);
  const EquirectToCubeOptions options { .face_size = kFace };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  const auto& cube = result.value();
  constexpr uint32_t kMid = kFace / 2;
  constexpr float kTolerance = 0.05F;
  // R holds the panorama's u, G its v.
  EXPECT_NEAR(CubeTexel(cube, 4, { kMid, kMid }, 0), 0.5F, kTolerance);
  EXPECT_NEAR(CubeTexel(cube, 4, { kMid, kMid }, 1), 0.5F, kTolerance);
  EXPECT_NEAR(CubeTexel(cube, 0, { kMid, kMid }, 0), 0.25F, kTolerance);
  EXPECT_NEAR(CubeTexel(cube, 1, { kMid, kMid }, 0), 0.75F, kTolerance);
  EXPECT_NEAR(CubeTexel(cube, 0, { kMid, kMid }, 1), 0.5F, kTolerance);
  EXPECT_LT(CubeTexel(cube, 2, { kMid, kMid }, 1), kTolerance);
  EXPECT_GT(CubeTexel(cube, 3, { kMid, kMid }, 1), 1.0F - kTolerance);
}

//! The panorama reads unmirrored from inside: on the forward face (index 4),
//! a viewer inside facing world -Y has world -X, the face's left column
//! (hardware -X), on their right, and that column holds the panorama's larger
//! u; the bottom row holds the larger v.
NOLINT_TEST(TextureSourceAssemblyTest, EquirectReadsUnmirroredFromInside)
{
  constexpr uint32_t kFace = 8;
  auto equirect = MakeGradientEquirect(256, 128);
  const EquirectToCubeOptions options { .face_size = kFace };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  const auto& cube = result.value();
  constexpr uint32_t kMid = kFace / 2;
  EXPECT_GT(CubeTexel(cube, 4, { 0, kMid }, 0),
    CubeTexel(cube, 4, { kFace - 1, kMid }, 0));
  EXPECT_GT(CubeTexel(cube, 4, { kMid, kFace - 1 }, 1),
    CubeTexel(cube, 4, { kMid, 0 }, 1));
}

//! ConvertEquirectangularToCube creates valid cube map from valid input.
NOLINT_TEST(TextureSourceAssemblyTest, ValidInputCreatesCubeMap)
{
  auto equirect = MakeEquirect({ .width = 64, .height = 32 });
  EquirectToCubeOptions options { .face_size = 16 };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  const auto& cube = result.value();
  EXPECT_TRUE(cube.IsValid());
  EXPECT_EQ(cube.Meta().texture_type, TextureType::kTextureCube);
  EXPECT_EQ(cube.Meta().width, 16);
  EXPECT_EQ(cube.Meta().height, 16);
  EXPECT_EQ(cube.Meta().array_layers, 6);
  EXPECT_EQ(cube.Meta().format, Format::kRGBA32Float);
}

//! ConvertEquirectangularToCube rejects invalid input image.
NOLINT_TEST(TextureSourceAssemblyTest, InvalidInputFails)
{
  const ScratchImage invalid {};
  const EquirectToCubeOptions options {};

  auto result = ConvertEquirectangularToCube(invalid, options);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDecodeFailed);
}

//! ConvertEquirectangularToCube rejects non-2:1 aspect ratio.
NOLINT_TEST(TextureSourceAssemblyTest, WrongAspectRatioFails)
{
  // 1:1 aspect ratio (square)
  auto square = MakeEquirect({ .width = 32, .height = 32 });
  const EquirectToCubeOptions options {};

  auto result = ConvertEquirectangularToCube(square, options);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! ConvertEquirectangularToCube rejects non-float formats.
NOLINT_TEST(TextureSourceAssemblyTest, NonFloatFormatFails)
{
  // RGBA8 format
  std::vector<std::byte> data(static_cast<size_t>(64) * 32 * 4);
  auto rgba8 = ScratchImage::CreateFromData(
    64, 32, Format::kRGBA8UNorm, 64 * 4, std::move(data));
  const EquirectToCubeOptions options {};

  auto result = ConvertEquirectangularToCube(rgba8, options);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidOutputFormat);
}

//! ConvertEquirectangularToCube rejects zero face size.
NOLINT_TEST(TextureSourceAssemblyTest, ZeroFaceSizeFails)
{
  auto equirect = MakeEquirect({ .width = 64, .height = 32 });
  EquirectToCubeOptions options { .face_size = 0 };

  auto result = ConvertEquirectangularToCube(equirect, options);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! ConvertEquirectangularToCube samples correct colors from solid equirect.
NOLINT_TEST(TextureSourceAssemblyTest, SolidColorPreservesColor)
{
  // Solid orange equirect
  auto equirect = MakeEquirect({ .width = 64, .height = 32 },
    { .r = 1.0F, .g = 0.5F, .b = 0.0F, .a = 1.0F });
  EquirectToCubeOptions options { .face_size = 4 };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  const auto& cube = result.value();

  // Check center pixel of +X face
  const auto face_view = cube.GetImage(0, 0); // face 0, mip 0
  const std::span<const std::byte> pixels = face_view.pixels;
  // Center pixel at (2, 2) in 4x4 face
  constexpr size_t kFaceSize = 4;
  constexpr size_t kChannels = 4;
  constexpr size_t center_idx = ((2 * kFaceSize) + 2) * kChannels;
  EXPECT_NEAR(ReadFloat(pixels, center_idx + 0), 1.0F, 0.1F); // R
  EXPECT_NEAR(ReadFloat(pixels, center_idx + 1), 0.5F, 0.1F); // G
  EXPECT_NEAR(ReadFloat(pixels, center_idx + 2), 0.0F, 0.1F); // B
  EXPECT_NEAR(ReadFloat(pixels, center_idx + 3), 1.0F, 0.1F); // A
}

//! ConvertEquirectangularToCube works with bilinear (box) filter.
NOLINT_TEST(TextureSourceAssemblyTest, BoxFilterWorks)
{
  auto equirect = MakeEquirect({ .width = 64, .height = 32 });
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kBox,
  };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(result.value().Meta().width, 8);
}

//! ConvertEquirectangularToCube works with Kaiser filter.
NOLINT_TEST(TextureSourceAssemblyTest, KaiserFilterWorks)
{
  auto equirect = MakeEquirect({ .width = 64, .height = 32 });
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kKaiser,
  };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(result.value().Meta().width, 8);
}

//! ConvertEquirectangularToCube works with Lanczos filter.
NOLINT_TEST(TextureSourceAssemblyTest, LanczosFilterWorks)
{
  auto equirect = MakeEquirect({ .width = 64, .height = 32 });
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kLanczos,
  };

  auto result = ConvertEquirectangularToCube(equirect, options);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(result.value().Meta().width, 8);
}

} // namespace
