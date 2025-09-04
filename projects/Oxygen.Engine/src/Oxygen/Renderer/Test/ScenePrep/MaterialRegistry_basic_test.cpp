//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Renderer/ScenePrep/State/MaterialRegistry.h>

namespace {

using oxygen::engine::sceneprep::MaterialHandle;
using oxygen::engine::sceneprep::MaterialRegistry;

//! Helper to create a minimal valid MaterialAsset instance
static auto MakeTestMaterial(const char* name = "TestMat")
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  oxygen::data::pak::MaterialAssetDesc desc {};
  // Safe copy of name (truncate if necessary)
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", name);
  desc.material_domain
    = static_cast<std::uint8_t>(oxygen::data::MaterialDomain::kOpaque);
  desc.flags = 0U;
  desc.shader_stages = 0U; // No shader refs for this simple test
  return std::make_shared<oxygen::data::MaterialAsset>(
    desc, std::vector<oxygen::data::ShaderReference> {});
}

//! Basic behavior tests for MaterialRegistry
NOLINT_TEST(MaterialRegistryBasicTest, GetOrRegister_ReusesHandleForSamePointer)
{
  // Arrange
  MaterialRegistry registry;
  auto mat = MakeTestMaterial();

  // Act
  const auto h1 = registry.GetOrRegisterMaterial(mat);
  const auto h2 = registry.GetOrRegisterMaterial(mat);

  // Assert
  EXPECT_EQ(h1.get(), h2.get());
  EXPECT_TRUE(registry.IsValidHandle(h1));
  EXPECT_FALSE(MaterialRegistry::IsSentinelHandle(h1));
  EXPECT_EQ(registry.GetRegisteredMaterialCount(), 1U);
}

//! Null material returns sentinel handle
NOLINT_TEST(MaterialRegistryBasicTest, NullMaterial_ReturnsSentinelHandle)
{
  // Arrange
  MaterialRegistry registry;

  // Act
  const auto h = registry.GetOrRegisterMaterial(nullptr);

  // Assert
  EXPECT_TRUE(MaterialRegistry::IsSentinelHandle(h));
  EXPECT_FALSE(registry.IsValidHandle(h));
}

//! Lookup without registration does not create entries
NOLINT_TEST(MaterialRegistryBasicTest, LookupMaterialHandle_NoRegistration)
{
  // Arrange
  MaterialRegistry registry;
  auto mat = MakeTestMaterial();

  // Act
  const auto lookup_before = registry.LookupMaterialHandle(mat.get());
  const auto h = registry.GetOrRegisterMaterial(mat);
  const auto lookup_after = registry.LookupMaterialHandle(mat.get());

  // Assert
  EXPECT_FALSE(lookup_before.has_value());
  ASSERT_TRUE(lookup_after.has_value());
  EXPECT_EQ(lookup_after->get(), h.get());
}

} // namespace
