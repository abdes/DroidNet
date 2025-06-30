//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/Loaders/ShaderLoader.h>
#include <Oxygen/Content/Test/Mocks/MockStream.h>
#include <Oxygen/Data/ShaderAsset.h>
#include <Oxygen/Data/ShaderType.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

using oxygen::content::loaders::LoadShaderAsset;
using oxygen::serio::Reader;

namespace {

//=== ShaderLoader Test Fixtures ===----------------------------------------//

//! Fixture for ShaderLoader basic serialization tests.
class ShaderLoaderBasicTestFixture : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  ShaderLoaderBasicTestFixture()
    : writer(stream)
    , reader(stream)
  {
  }

  MockStream stream;
  Writer writer;
  Reader<MockStream> reader;
};

//=== ShaderLoader Basic Functionality Tests ===----------------------------//

//! Test: LoadShader returns valid ShaderAsset for correct input.
NOLINT_TEST_F(
  ShaderLoaderBasicTestFixture, LoadShader_ValidInput_ReturnsShaderAsset)
{
  // Arrange
  using oxygen::data::ShaderAsset;
  using oxygen::data::ShaderType;

  uint32_t shader_type = static_cast<uint32_t>(ShaderType::kVertex);
  std::string name = "TestShader";
  ASSERT_TRUE(writer.write(shader_type));
  ASSERT_TRUE(writer.write_string(name));
  stream.seek(0);

  // Act
  auto asset = LoadShaderAsset(reader);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetTypeId(), ShaderAsset::ClassTypeId());
  EXPECT_EQ(asset->GetShaderType(), ShaderType::kVertex);
  EXPECT_EQ(asset->GetShaderName(), name);
}

//=== ShaderLoader Error Handling Tests ===--------------------------------//

//! Fixture for ShaderLoader error test cases.
class ShaderLoaderErrorTestFixture : public ShaderLoaderBasicTestFixture {
  // No additional members needed for now; extend as needed for error scenarios.
};

//! Test: LoadShader throws if shader_type cannot be read.
NOLINT_TEST_F(
  ShaderLoaderErrorTestFixture, LoadShader_FailsToReadShaderType_Throws)
{
  // Arrange

  // Act + Assert
  EXPECT_THROW(
    { (void)LoadShaderAsset(std::move(reader)); }, std::runtime_error);
}

//! Test: LoadShader throws if shader_name cannot be read.
NOLINT_TEST_F(
  ShaderLoaderErrorTestFixture, LoadShader_FailsToReadShaderName_Throws)
{
  // Arrange
  using oxygen::data::ShaderType;

  // Write only shader_type, but not name
  uint32_t shader_type = static_cast<uint32_t>(ShaderType::kGeometry);
  stream.write(
    reinterpret_cast<const std::byte*>(&shader_type), sizeof(shader_type));
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW({ (void)LoadShaderAsset(reader); }, std::runtime_error);
}

} // namespace
