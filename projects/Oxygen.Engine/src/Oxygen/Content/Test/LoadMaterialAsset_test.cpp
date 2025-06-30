//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Test/Mocks/MockStream.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::IsSupersetOf;
using ::testing::NotNull;
using ::testing::SizeIs;

using oxygen::content::loaders::LoadMaterialAsset;
using oxygen::serio::Reader;

namespace {

//=== MaterialLoader Test Fixtures ===-------------------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTestFixture : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  MaterialLoaderBasicTestFixture()
    : writer(stream)
    , reader(stream)
  {
  }

  MockStream stream;
  Writer writer;
  Reader<MockStream> reader;
};

//=== MaterialLoader Basic Functionality Tests ===-------------------------//

//! Test: LoadMaterialAsset returns valid MaterialAsset for correct input.
NOLINT_TEST_F(
  MaterialLoaderBasicTestFixture, LoadMaterial_ValidInput_ReturnsMaterialAsset)
{
  // Arrange
  using oxygen::data::MaterialAsset;
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52] {};
  };
  constexpr uint32_t kMaterialType = 42;
  constexpr uint32_t kShaderStages = 0b1011; // 3 bits set
  constexpr uint32_t kTextureCount = 2;
  const std::vector<uint64_t> shader_ids = { 11, 22, 33 };
  const std::vector<uint64_t> texture_ids = { 44, 55 };

  std::vector<std::byte> buffer(sizeof(MaterialAssetHeader)
    + shader_ids.size() * 8 + texture_ids.size() * 8);
  auto* header = reinterpret_cast<MaterialAssetHeader*>(buffer.data());
  header->material_type = kMaterialType;
  header->shader_stages = kShaderStages;
  header->texture_count = kTextureCount;
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader), shader_ids.data(),
    shader_ids.size() * 8);
  std::memcpy(
    buffer.data() + sizeof(MaterialAssetHeader) + shader_ids.size() * 8,
    texture_ids.data(), texture_ids.size() * 8);

  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act
  auto asset = LoadMaterialAsset(reader);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetMaterialType(), kMaterialType);
  EXPECT_EQ(asset->GetShaderStages(), kShaderStages);
  EXPECT_EQ(asset->GetTextureCount(), kTextureCount);
  EXPECT_THAT(asset->GetShaderIds(),
    AllOf(SizeIs(3), IsSupersetOf({ 11ull, 22ull, 33ull })));
  EXPECT_THAT(
    asset->GetTextureIds(), AllOf(SizeIs(2), IsSupersetOf({ 44ull, 55ull })));
}

//=== MaterialLoader Error Handling Tests ===------------------------------//

//! Fixture for MaterialLoader error test cases.
class MaterialLoaderErrorTestFixture : public MaterialLoaderBasicTestFixture {
  // No additional members needed for now; extend as needed for error scenarios.
};

//! Test: LoadMaterialAsset throws if header cannot be read.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadHeader_Throws)
{
  // Arrange
  std::vector<std::byte> buffer(10, std::byte { 0 }); // Too small
  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW({ (void)LoadMaterialAsset(reader); }, std::runtime_error);
}

//! Test: LoadMaterialAsset throws if shader IDs are truncated.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadShaderIds_Throws)
{
  // Arrange
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52] {};
  };
  constexpr uint32_t kMaterialType = 1;
  constexpr uint32_t kShaderStages = 0b111; // 3 bits set
  constexpr uint32_t kTextureCount = 0;
  std::vector<std::byte> buffer(
    sizeof(MaterialAssetHeader) + 8 * 2); // only 2 shader IDs, should be 3
  auto* header = reinterpret_cast<MaterialAssetHeader*>(buffer.data());
  header->material_type = kMaterialType;
  header->shader_stages = kShaderStages;
  header->texture_count = kTextureCount;
  uint64_t shader_ids[2] = { 123, 456 };
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader), shader_ids, 2 * 8);

  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW({ (void)LoadMaterialAsset(reader); }, std::runtime_error);
}

//! Test: LoadMaterialAsset throws if texture IDs are truncated.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadTextureIds_Throws)
{
  // Arrange
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52] {};
  };
  constexpr uint32_t kMaterialType = 1;
  constexpr uint32_t kShaderStages = 0b1; // 1 shader ID
  constexpr uint32_t kTextureCount = 2;
  std::vector<std::byte> buffer(sizeof(MaterialAssetHeader) + 8 * 1
    + 8 * 1); // only 1 texture ID, should be 2
  auto* header = reinterpret_cast<MaterialAssetHeader*>(buffer.data());
  header->material_type = kMaterialType;
  header->shader_stages = kShaderStages;
  header->texture_count = kTextureCount;
  uint64_t shader_id = 123;
  uint64_t texture_id = 456;
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader), &shader_id, 8);
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader) + 8, &texture_id, 8);

  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW({ (void)LoadMaterialAsset(reader); }, std::runtime_error);
}

} // namespace
