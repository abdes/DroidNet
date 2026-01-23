//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

using namespace oxygen::content::import;
using oxygen::Format;
using oxygen::TextureType;

namespace {

//=== CubeFace Tests
//===----------------------------------------------------===//

class CubeFaceTest : public ::testing::Test { };

//! to_string returns the expected string for all cube face values.
NOLINT_TEST_F(CubeFaceTest, ToString_ReturnsExpectedStrings)
{
  // Act & Assert
  EXPECT_STREQ(to_string(CubeFace::kPositiveX), "PositiveX");
  EXPECT_STREQ(to_string(CubeFace::kNegativeX), "NegativeX");
  EXPECT_STREQ(to_string(CubeFace::kPositiveY), "PositiveY");
  EXPECT_STREQ(to_string(CubeFace::kNegativeY), "NegativeY");
  EXPECT_STREQ(to_string(CubeFace::kPositiveZ), "PositiveZ");
  EXPECT_STREQ(to_string(CubeFace::kNegativeZ), "NegativeZ");
}

//! CubeFace enum values match D3D12/Vulkan face ordering (0-5).
NOLINT_TEST_F(CubeFaceTest, EnumValues_MatchExpectedOrdering)
{
  // Assert
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveX), 0);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeX), 1);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveY), 2);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeY), 3);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kPositiveZ), 4);
  EXPECT_EQ(static_cast<uint8_t>(CubeFace::kNegativeZ), 5);
}

//! kCubeFaceCount constant equals 6.
NOLINT_TEST_F(CubeFaceTest, CubeFaceCount_IsSix)
{
  // Assert
  EXPECT_EQ(kCubeFaceCount, 6);
}

//=== SubresourceId Tests
//===-----------------------------------------------===//

class SubresourceIdTest : public ::testing::Test { };

//! Default-constructed SubresourceId has all fields zero.
NOLINT_TEST_F(SubresourceIdTest, DefaultConstruction_AllFieldsZero)
{
  // Arrange & Act
  const SubresourceId id {};

  // Assert
  EXPECT_EQ(id.array_layer, 0);
  EXPECT_EQ(id.mip_level, 0);
  EXPECT_EQ(id.depth_slice, 0);
}

//! SubresourceId equality comparison works correctly.
NOLINT_TEST_F(SubresourceIdTest, Equality_ComparesAllFields)
{
  // Arrange
  const SubresourceId id1 {
    .array_layer = 1, .mip_level = 2, .depth_slice = 3
  };
  const SubresourceId id2 {
    .array_layer = 1, .mip_level = 2, .depth_slice = 3
  };
  const SubresourceId id3 {
    .array_layer = 0, .mip_level = 2, .depth_slice = 3
  };

  // Act & Assert
  EXPECT_EQ(id1, id2);
  EXPECT_NE(id1, id3);
}

//=== TextureSourceSet Basic Tests
//===--------------------------------------===//

class TextureSourceSetBasicTest : public ::testing::Test { };

//! Default-constructed TextureSourceSet is empty.
NOLINT_TEST_F(TextureSourceSetBasicTest, DefaultConstruction_IsEmpty)
{
  // Arrange & Act
  const TextureSourceSet set;

  // Assert
  EXPECT_TRUE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 0);
  EXPECT_TRUE(set.Sources().empty());
}

//! Add() adds a source to the set.
NOLINT_TEST_F(TextureSourceSetBasicTest, Add_AddsSource)
{
  // Arrange
  TextureSourceSet set;
  std::vector<std::byte> bytes { std::byte { 0x01 }, std::byte { 0x02 } };

  // Act
  set.Add(TextureSource {
    .bytes = bytes,
    .subresource = SubresourceId { .array_layer = 1, .mip_level = 2 },
    .source_id = "test.png",
  });

  // Assert
  EXPECT_FALSE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 1);
}

//! Clear() removes all sources.
NOLINT_TEST_F(TextureSourceSetBasicTest, Clear_RemovesAllSources)
{
  // Arrange
  TextureSourceSet set;
  set.Add(TextureSource {
    .bytes = { std::byte { 0x01 } },
    .subresource = {},
    .source_id = "test.png",
  });
  ASSERT_FALSE(set.IsEmpty());

  // Act
  set.Clear();

  // Assert
  EXPECT_TRUE(set.IsEmpty());
  EXPECT_EQ(set.Count(), 0);
}

//! GetSource() returns the source at the given index.
NOLINT_TEST_F(TextureSourceSetBasicTest, GetSource_ReturnsCorrectSource)
{
  // Arrange
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

  // Act
  const auto& source0 = set.GetSource(0);
  const auto& source1 = set.GetSource(1);

  // Assert
  EXPECT_EQ(source0.source_id, "first.png");
  EXPECT_EQ(source0.subresource.array_layer, 5);
  EXPECT_EQ(source1.source_id, "second.png");
  EXPECT_EQ(source1.subresource.array_layer, 7);
}

//! GetSource() throws when index is out of range.
NOLINT_TEST_F(TextureSourceSetBasicTest, GetSource_ThrowsOnOutOfRange)
{
  // Arrange
  const TextureSourceSet set;

  // Act & Assert
  EXPECT_THROW((void)set.GetSource(0), std::out_of_range);
}

//=== TextureSourceSet Array Layer Tests
//===---------------------------------===//

class TextureSourceSetArrayLayerTest : public ::testing::Test { };

//! AddArrayLayer() sets correct subresource fields.
NOLINT_TEST_F(TextureSourceSetArrayLayerTest, AddArrayLayer_SetsCorrectFields)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddArrayLayer(3, { std::byte { 0x01 } }, "layer3.png");

  // Assert
  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 3);
  EXPECT_EQ(source.subresource.mip_level, 0);
  EXPECT_EQ(source.subresource.depth_slice, 0);
  EXPECT_EQ(source.source_id, "layer3.png");
}

//! Multiple AddArrayLayer() calls create separate sources.
NOLINT_TEST_F(TextureSourceSetArrayLayerTest, AddArrayLayer_MultipleLayers)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddArrayLayer(0, { std::byte { 0x00 } }, "layer0.png");
  set.AddArrayLayer(1, { std::byte { 0x01 } }, "layer1.png");
  set.AddArrayLayer(2, { std::byte { 0x02 } }, "layer2.png");

  // Assert
  EXPECT_EQ(set.Count(), 3);
  EXPECT_EQ(set.GetSource(0).subresource.array_layer, 0);
  EXPECT_EQ(set.GetSource(1).subresource.array_layer, 1);
  EXPECT_EQ(set.GetSource(2).subresource.array_layer, 2);
}

//=== TextureSourceSet Cube Face Tests
//===----------------------------------===//

class TextureSourceSetCubeFaceTest : public ::testing::Test { };

//! AddCubeFace() maps face to correct array layer index.
NOLINT_TEST_F(TextureSourceSetCubeFaceTest, AddCubeFace_MapsToArrayLayer)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddCubeFace(CubeFace::kPositiveX, { std::byte { 0x00 } }, "px.hdr");
  set.AddCubeFace(CubeFace::kNegativeZ, { std::byte { 0x05 } }, "nz.hdr");

  // Assert
  EXPECT_EQ(set.GetSource(0).subresource.array_layer, 0); // kPositiveX = 0
  EXPECT_EQ(set.GetSource(1).subresource.array_layer, 5); // kNegativeZ = 5
}

//! AddCubeFace() for all 6 faces creates correct mapping.
NOLINT_TEST_F(TextureSourceSetCubeFaceTest, AddAllFaces_CreatesCompleteCube)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddCubeFace(CubeFace::kPositiveX, { std::byte { 0x00 } }, "px.hdr");
  set.AddCubeFace(CubeFace::kNegativeX, { std::byte { 0x01 } }, "nx.hdr");
  set.AddCubeFace(CubeFace::kPositiveY, { std::byte { 0x02 } }, "py.hdr");
  set.AddCubeFace(CubeFace::kNegativeY, { std::byte { 0x03 } }, "ny.hdr");
  set.AddCubeFace(CubeFace::kPositiveZ, { std::byte { 0x04 } }, "pz.hdr");
  set.AddCubeFace(CubeFace::kNegativeZ, { std::byte { 0x05 } }, "nz.hdr");

  // Assert
  EXPECT_EQ(set.Count(), 6);
  for (size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.array_layer, i);
  }
}

//=== TextureSourceSet Depth Slice Tests
//===---------------------------------===//

class TextureSourceSetDepthSliceTest : public ::testing::Test { };

//! AddDepthSlice() sets correct subresource fields.
NOLINT_TEST_F(TextureSourceSetDepthSliceTest, AddDepthSlice_SetsCorrectFields)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddDepthSlice(7, { std::byte { 0x07 } }, "slice7.png");

  // Assert
  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 0);
  EXPECT_EQ(source.subresource.mip_level, 0);
  EXPECT_EQ(source.subresource.depth_slice, 7);
  EXPECT_EQ(source.source_id, "slice7.png");
}

//! Multiple AddDepthSlice() calls create separate sources.
NOLINT_TEST_F(TextureSourceSetDepthSliceTest, AddDepthSlice_MultipleSlices)
{
  // Arrange
  TextureSourceSet set;

  // Act
  for (uint16_t i = 0; i < 16; ++i) {
    set.AddDepthSlice(
      i, { static_cast<std::byte>(i) }, "slice" + std::to_string(i) + ".png");
  }

  // Assert
  EXPECT_EQ(set.Count(), 16);
  for (size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.depth_slice, i);
  }
}

//=== TextureSourceSet Mip Level Tests
//===----------------------------------===//

class TextureSourceSetMipLevelTest : public ::testing::Test { };

//! AddMipLevel() sets correct subresource fields.
NOLINT_TEST_F(TextureSourceSetMipLevelTest, AddMipLevel_SetsCorrectFields)
{
  // Arrange
  TextureSourceSet set;

  // Act
  set.AddMipLevel(2, 3, { std::byte { 0x23 } }, "layer2_mip3.png");

  // Assert
  const auto& source = set.GetSource(0);
  EXPECT_EQ(source.subresource.array_layer, 2);
  EXPECT_EQ(source.subresource.mip_level, 3);
  EXPECT_EQ(source.subresource.depth_slice, 0);
  EXPECT_EQ(source.source_id, "layer2_mip3.png");
}

//! AddMipLevel() supports pre-authored mip chains.
NOLINT_TEST_F(TextureSourceSetMipLevelTest, AddMipLevel_FullMipChain)
{
  // Arrange
  TextureSourceSet set;

  // Act - Add mip chain for a single array layer
  set.AddMipLevel(0, 0, { std::byte { 0x00 } }, "mip0.png");
  set.AddMipLevel(0, 1, { std::byte { 0x01 } }, "mip1.png");
  set.AddMipLevel(0, 2, { std::byte { 0x02 } }, "mip2.png");
  set.AddMipLevel(0, 3, { std::byte { 0x03 } }, "mip3.png");

  // Assert
  EXPECT_EQ(set.Count(), 4);
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(set.GetSource(i).subresource.array_layer, 0);
    EXPECT_EQ(set.GetSource(i).subresource.mip_level, i);
  }
}

//=== TextureSourceSet Sources() Tests
//===----------------------------------===//

class TextureSourceSetSourcesTest : public ::testing::Test { };

//! Sources() returns a span over all added sources.
NOLINT_TEST_F(TextureSourceSetSourcesTest, Sources_ReturnsAllSources)
{
  // Arrange
  TextureSourceSet set;
  set.AddArrayLayer(0, { std::byte { 0x00 } }, "a.png");
  set.AddArrayLayer(1, { std::byte { 0x01 } }, "b.png");
  set.AddArrayLayer(2, { std::byte { 0x02 } }, "c.png");

  // Act
  const auto sources = set.Sources();

  // Assert
  EXPECT_EQ(sources.size(), 3);
  EXPECT_EQ(sources[0].source_id, "a.png");
  EXPECT_EQ(sources[1].source_id, "b.png");
  EXPECT_EQ(sources[2].source_id, "c.png");
}

//=== EquirectToCubeOptions Tests
//===---------------------------------------===//

class EquirectToCubeOptionsTest : public ::testing::Test { };

//! Default EquirectToCubeOptions has expected values.
NOLINT_TEST_F(EquirectToCubeOptionsTest, DefaultValues_AreCorrect)
{
  // Arrange & Act
  const EquirectToCubeOptions options {};

  // Assert
  EXPECT_EQ(options.face_size, 512);
  EXPECT_EQ(options.sample_filter, MipFilter::kKaiser);
}

//=== CubeFaceBasis Tests
//===-----------------------------------------------===//

class CubeFaceBasisTest : public ::testing::Test { };

//! kCubeFaceBases has correct size for all 6 faces.
NOLINT_TEST_F(CubeFaceBasisTest, ArrayHasSixEntries)
{
  // Assert
  EXPECT_EQ(kCubeFaceBases.size(), 6);
}

//! +X face has correct basis vectors.
NOLINT_TEST_F(CubeFaceBasisTest, PositiveX_HasCorrectBasis)
{
  // Arrange
  const auto& basis = GetCubeFaceBasis(CubeFace::kPositiveX);

  // Assert - center (+1, 0, 0), right (0, +1, 0), up (0, 0, +1)
  EXPECT_FLOAT_EQ(basis.center.x, 1.0F);
  EXPECT_FLOAT_EQ(basis.center.y, 0.0F);
  EXPECT_FLOAT_EQ(basis.center.z, 0.0F);
  EXPECT_FLOAT_EQ(basis.right.x, 0.0F);
  EXPECT_FLOAT_EQ(basis.right.y, 1.0F);
  EXPECT_FLOAT_EQ(basis.right.z, 0.0F);
  EXPECT_FLOAT_EQ(basis.up.x, 0.0F);
  EXPECT_FLOAT_EQ(basis.up.y, 0.0F);
  EXPECT_FLOAT_EQ(basis.up.z, 1.0F);
}

//! +Z face has correct basis vectors (up face in Z-up coordinate system).
NOLINT_TEST_F(CubeFaceBasisTest, PositiveZ_HasCorrectBasis)
{
  // Arrange
  const auto& basis = GetCubeFaceBasis(CubeFace::kPositiveZ);

  // Assert - center (0, 0, +1), right (+1, 0, 0), up (0, -1, 0)
  EXPECT_FLOAT_EQ(basis.center.x, 0.0F);
  EXPECT_FLOAT_EQ(basis.center.y, 0.0F);
  EXPECT_FLOAT_EQ(basis.center.z, 1.0F);
  EXPECT_FLOAT_EQ(basis.right.x, 1.0F);
  EXPECT_FLOAT_EQ(basis.right.y, 0.0F);
  EXPECT_FLOAT_EQ(basis.right.z, 0.0F);
  EXPECT_FLOAT_EQ(basis.up.x, 0.0F);
  EXPECT_FLOAT_EQ(basis.up.y, -1.0F);
  EXPECT_FLOAT_EQ(basis.up.z, 0.0F);
}

//=== ComputeCubeDirection Tests
//===-----------------------------------------===//

class ComputeCubeDirectionTest : public ::testing::Test { };

//! ComputeCubeDirection at face center (0.5, 0.5) returns the face normal.
NOLINT_TEST_F(ComputeCubeDirectionTest, AtCenter_ReturnsFaceNormal)
{
  // Arrange & Act
  const auto dir_px = ComputeCubeDirection(CubeFace::kPositiveX, 0.5F, 0.5F);
  const auto dir_pz = ComputeCubeDirection(CubeFace::kPositiveZ, 0.5F, 0.5F);
  const auto dir_ny = ComputeCubeDirection(CubeFace::kNegativeY, 0.5F, 0.5F);

  // Assert - at center, direction equals the face normal
  EXPECT_FLOAT_EQ(dir_px.x, 1.0F);
  EXPECT_FLOAT_EQ(dir_px.y, 0.0F);
  EXPECT_FLOAT_EQ(dir_px.z, 0.0F);

  EXPECT_FLOAT_EQ(dir_pz.x, 0.0F);
  EXPECT_FLOAT_EQ(dir_pz.y, 0.0F);
  EXPECT_FLOAT_EQ(dir_pz.z, 1.0F);

  EXPECT_FLOAT_EQ(dir_ny.x, 0.0F);
  EXPECT_FLOAT_EQ(dir_ny.y, -1.0F);
  EXPECT_FLOAT_EQ(dir_ny.z, 0.0F);
}

//! ComputeCubeDirection returns normalized vectors.
NOLINT_TEST_F(ComputeCubeDirectionTest, ReturnsNormalizedVectors)
{
  // Arrange - sample at corner (not unit direction before normalization)
  const auto dir = ComputeCubeDirection(CubeFace::kPositiveX, 0.0F, 0.0F);

  // Act - compute magnitude
  const float magnitude
    = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

  // Assert - should be 1.0 (normalized)
  EXPECT_NEAR(magnitude, 1.0F, 1e-6F);
}

//! ComputeCubeDirection at corner points toward cube corner.
NOLINT_TEST_F(ComputeCubeDirectionTest, AtCorner_PointsTowardCubeCorner)
{
  // Arrange & Act - bottom-left corner of +X face
  // UV (0, 0) -> s=-1, t=-1 -> direction = center + (-1)*right + (-1)*up
  // For +X: center=(1,0,0), right=(0,1,0), up=(0,0,1)
  // Result: (1, -1, -1) normalized
  const auto dir = ComputeCubeDirection(CubeFace::kPositiveX, 0.0F, 0.0F);

  // Assert - direction should have positive X, negative Y, negative Z
  EXPECT_GT(dir.x, 0.0F);
  EXPECT_LT(dir.y, 0.0F);
  EXPECT_LT(dir.z, 0.0F);
}

//=== AssembleCubeFromFaces Tests
//===-----------------------------------------===//

class AssembleCubeFromFacesTest : public ::testing::Test {
protected:
  //! Create a simple 2x2 RGBA8 test face with a unique color.
  static auto MakeTestFace(uint8_t color_value) -> ScratchImage
  {
    constexpr uint32_t kWidth = 2;
    constexpr uint32_t kHeight = 2;
    constexpr uint32_t kBytesPerPixel = 4;
    constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
    std::vector<std::byte> data(
      kWidth * kHeight * kBytesPerPixel, std::byte { color_value });
    return ScratchImage::CreateFromData(
      kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(data));
  }
};

//! AssembleCubeFromFaces with valid faces creates a cube map.
NOLINT_TEST_F(AssembleCubeFromFacesTest, ValidFaces_CreatesCubeMap)
{
  // Arrange
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x00),
    MakeTestFace(0x11),
    MakeTestFace(0x22),
    MakeTestFace(0x33),
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  // Act
  auto result = AssembleCubeFromFaces(faces);

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->IsValid());
  const auto& meta = result->Meta();
  EXPECT_EQ(meta.width, 2);
  EXPECT_EQ(meta.height, 2);
  EXPECT_EQ(meta.array_layers, 6);
  EXPECT_EQ(meta.texture_type, TextureType::kTextureCube);
}

//! AssembleCubeFromFaces copies face data correctly.
NOLINT_TEST_F(AssembleCubeFromFacesTest, CopiesFaceDataCorrectly)
{
  // Arrange - each face has unique pixel data
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x10),
    MakeTestFace(0x20),
    MakeTestFace(0x30),
    MakeTestFace(0x40),
    MakeTestFace(0x50),
    MakeTestFace(0x60),
  };

  // Act
  auto result = AssembleCubeFromFaces(faces);
  ASSERT_TRUE(result.has_value());

  // Assert - verify each face has the expected data
  for (uint16_t i = 0; i < 6; ++i) {
    const auto image = result->GetImage(i, 0);
    const uint8_t expected = static_cast<uint8_t>((i + 1) * 0x10);
    EXPECT_EQ(static_cast<uint8_t>(image.pixels[0]), expected);
  }
}

//! AssembleCubeFromFaces fails with invalid face.
NOLINT_TEST_F(AssembleCubeFromFacesTest, InvalidFace_Fails)
{
  // Arrange - one invalid face
  std::array<ScratchImage, 6> faces = {
    MakeTestFace(0x00),
    MakeTestFace(0x11),
    MakeTestFace(0x22),
    ScratchImage {}, // Invalid face
    MakeTestFace(0x44),
    MakeTestFace(0x55),
  };

  // Act
  auto result = AssembleCubeFromFaces(faces);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! AssembleCubeFromFaces fails with non-square face.
NOLINT_TEST_F(AssembleCubeFromFacesTest, NonSquareFace_Fails)
{
  // Arrange - create a non-square face (4x2)
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 2; // Non-square
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
  std::vector<std::byte> data(kWidth * kHeight * kBytesPerPixel);
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

  // Act
  auto result = AssembleCubeFromFaces(faces);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! AssembleCubeFromFaces fails with mismatched dimensions.
NOLINT_TEST_F(AssembleCubeFromFacesTest, MismatchedDimensions_Fails)
{
  // Arrange - create a larger face (4x4 vs 2x2)
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 4;
  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kRowPitch = kWidth * kBytesPerPixel;
  std::vector<std::byte> large_data(kWidth * kHeight * kBytesPerPixel);
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

  // Act
  auto result = AssembleCubeFromFaces(faces);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDimensionMismatch);
}

//=== ConvertEquirectangularToCube Tests
//===---------------------------------===//

class ConvertEquirectangularToCubeTest : public ::testing::Test {
protected:
  //! Create a test equirectangular image (2:1 aspect, RGBA32F).
  static auto MakeEquirect(const uint32_t width, const uint32_t height,
    const float r = 1.0F, const float g = 0.5F, const float b = 0.0F,
    const float a = 1.0F) -> ScratchImage
  {
    const uint32_t pixel_count = width * height;
    std::vector<std::byte> data(pixel_count * 16); // 4 floats per pixel
    auto* floats = reinterpret_cast<float*>(data.data());
    for (uint32_t i = 0; i < pixel_count; ++i) {
      floats[i * 4 + 0] = r;
      floats[i * 4 + 1] = g;
      floats[i * 4 + 2] = b;
      floats[i * 4 + 3] = a;
    }
    return ScratchImage::CreateFromData(
      width, height, Format::kRGBA32Float, width * 16, std::move(data));
  }

  //! Create a test equirectangular with gradient (longitude varies R, lat G).
  static auto MakeGradientEquirect(const uint32_t width, const uint32_t height)
    -> ScratchImage
  {
    std::vector<std::byte> data(width * height * 16);
    auto* floats = reinterpret_cast<float*>(data.data());
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
        // R = horizontal position (longitude)
        floats[idx + 0] = static_cast<float>(x) / static_cast<float>(width - 1);
        // G = vertical position (latitude)
        floats[idx + 1]
          = static_cast<float>(y) / static_cast<float>(height - 1);
        floats[idx + 2] = 0.0F;
        floats[idx + 3] = 1.0F;
      }
    }
    return ScratchImage::CreateFromData(
      width, height, Format::kRGBA32Float, width * 16, std::move(data));
  }
};

//! ConvertEquirectangularToCube creates valid cube map from valid input.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, ValidInput_CreatesCubeMap)
{
  // Arrange
  auto equirect = MakeEquirect(64, 32);
  EquirectToCubeOptions options { .face_size = 16 };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  const auto& cube = result.value();
  EXPECT_TRUE(cube.IsValid());
  EXPECT_EQ(cube.Meta().texture_type, TextureType::kTextureCube);
  EXPECT_EQ(cube.Meta().width, 16);
  EXPECT_EQ(cube.Meta().height, 16);
  EXPECT_EQ(cube.Meta().array_layers, 6);
  EXPECT_EQ(cube.Meta().format, Format::kRGBA32Float);
}

//! ConvertEquirectangularToCube rejects invalid input image.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, InvalidInput_Fails)
{
  // Arrange
  const ScratchImage invalid {};
  const EquirectToCubeOptions options {};

  // Act
  auto result = ConvertEquirectangularToCube(invalid, options);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDecodeFailed);
}

//! ConvertEquirectangularToCube rejects non-2:1 aspect ratio.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, WrongAspectRatio_Fails)
{
  // Arrange - 1:1 aspect ratio (square)
  auto square = MakeEquirect(32, 32);
  const EquirectToCubeOptions options {};

  // Act
  auto result = ConvertEquirectangularToCube(square, options);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! ConvertEquirectangularToCube rejects non-float formats.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, NonFloatFormat_Fails)
{
  // Arrange - RGBA8 format
  std::vector<std::byte> data(64 * 32 * 4);
  auto rgba8 = ScratchImage::CreateFromData(
    64, 32, Format::kRGBA8UNorm, 64 * 4, std::move(data));
  const EquirectToCubeOptions options {};

  // Act
  auto result = ConvertEquirectangularToCube(rgba8, options);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidOutputFormat);
}

//! ConvertEquirectangularToCube rejects zero face size.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, ZeroFaceSize_Fails)
{
  // Arrange
  auto equirect = MakeEquirect(64, 32);
  EquirectToCubeOptions options { .face_size = 0 };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! ConvertEquirectangularToCube samples correct colors from solid equirect.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, SolidColor_PreservesColor)
{
  // Arrange - solid orange equirect
  auto equirect = MakeEquirect(64, 32, 1.0F, 0.5F, 0.0F, 1.0F);
  EquirectToCubeOptions options { .face_size = 4 };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  const auto& cube = result.value();

  // Check center pixel of +X face
  const auto face_view = cube.GetImage(0, 0); // face 0, mip 0
  const auto* pixels = reinterpret_cast<const float*>(face_view.pixels.data());
  // Center pixel at (2, 2) in 4x4 face
  const size_t center_idx = (2 * 4 + 2) * 4;
  EXPECT_NEAR(pixels[center_idx + 0], 1.0F, 0.1F); // R
  EXPECT_NEAR(pixels[center_idx + 1], 0.5F, 0.1F); // G
  EXPECT_NEAR(pixels[center_idx + 2], 0.0F, 0.1F); // B
  EXPECT_NEAR(pixels[center_idx + 3], 1.0F, 0.1F); // A
}

//! ConvertEquirectangularToCube works with bilinear (box) filter.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, BoxFilter_Works)
{
  // Arrange
  auto equirect = MakeEquirect(64, 32);
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kBox,
  };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().Meta().width, 8);
}

//! ConvertEquirectangularToCube works with Kaiser filter.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, KaiserFilter_Works)
{
  // Arrange
  auto equirect = MakeEquirect(64, 32);
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kKaiser,
  };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().Meta().width, 8);
}

//! ConvertEquirectangularToCube works with Lanczos filter.
NOLINT_TEST_F(ConvertEquirectangularToCubeTest, LanczosFilter_Works)
{
  // Arrange
  auto equirect = MakeEquirect(64, 32);
  EquirectToCubeOptions options {
    .face_size = 8,
    .sample_filter = MipFilter::kLanczos,
  };

  // Act
  auto result = ConvertEquirectangularToCube(equirect, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().Meta().width, 8);
}

} // namespace
