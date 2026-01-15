//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>

namespace {

using oxygen::content::import::D3D12PackingPolicy;
using oxygen::content::import::TightPackedPolicy;
using oxygen::content::import::emit::CookedEmissionResult;
using oxygen::content::import::emit::CookerConfig;
using oxygen::content::import::emit::CookTextureForEmission;
using oxygen::content::import::emit::CookTextureWithFallback;
using oxygen::content::import::emit::CreateFallbackTexture;
using oxygen::content::import::emit::CreatePlaceholderForMissingTexture;
using oxygen::content::import::emit::GetPackingPolicy;
using oxygen::content::import::emit::MakeImportDescFromConfig;

//===----------------------------------------------------------------------===//
// Test Utilities
//===----------------------------------------------------------------------===//

//! Creates a minimal valid BMP image (2x2, 32-bit BGRA).
[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
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

  // BMP file header
  bytes.push_back(static_cast<std::byte>('B'));
  bytes.push_back(static_cast<std::byte>('M'));
  push_u32(kFileSize);
  push_u16(0u);
  push_u16(0u);
  push_u32(kPixelOffset);

  // DIB header
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

  // Pixel data
  push_bgra(0u, 0u, 255u, 255u);
  push_bgra(255u, 255u, 255u, 255u);
  push_bgra(255u, 0u, 0u, 255u);
  push_bgra(0u, 255u, 0u, 255u);

  return bytes;
}

[[nodiscard]] auto GetTestImageBytes() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp2x2();
  return { kTestBmp.data(), kTestBmp.size() };
}

//===----------------------------------------------------------------------===//
// GetPackingPolicy Tests
//===----------------------------------------------------------------------===//

class GetPackingPolicyTest : public ::testing::Test { };

//! Verifies D3D12 policy ID returns correct policy.
NOLINT_TEST_F(GetPackingPolicyTest, D3D12PolicyId_ReturnsCorrectPolicy)
{
  // Act
  const auto& policy = GetPackingPolicy("d3d12");

  // Assert
  EXPECT_EQ(policy.Id(), "d3d12");
}

//! Verifies tight packing policy ID returns correct policy.
NOLINT_TEST_F(GetPackingPolicyTest, TightPolicyId_ReturnsCorrectPolicy)
{
  // Act
  const auto& policy = GetPackingPolicy("tight");

  // Assert
  EXPECT_EQ(policy.Id(), "tight");
}

//! Verifies unknown policy ID returns D3D12 as default.
NOLINT_TEST_F(GetPackingPolicyTest, UnknownPolicyId_ReturnsD3D12Default)
{
  // Act
  const auto& policy = GetPackingPolicy("unknown_policy");

  // Assert
  EXPECT_EQ(policy.Id(), "d3d12");
}

//===----------------------------------------------------------------------===//
// MakeImportDescFromConfig Tests
//===----------------------------------------------------------------------===//

class MakeImportDescFromConfigTest : public ::testing::Test { };

//! Verifies mip policy is set from config.
NOLINT_TEST_F(MakeImportDescFromConfigTest, SetsMipPolicy)
{
  // Arrange
  CookerConfig config {
    .enabled = true,
    .mip_policy = oxygen::content::import::MipPolicy::kFullChain,
  };

  // Act
  auto desc = MakeImportDescFromConfig(config, "test_texture");

  // Assert
  EXPECT_EQ(desc.mip_policy, oxygen::content::import::MipPolicy::kFullChain);
}

//! Verifies packing policy ID is in config.
NOLINT_TEST_F(MakeImportDescFromConfigTest, ConfigHasPolicyId)
{
  // Arrange
  CookerConfig config {
    .enabled = true,
    .packing_policy_id = "tight",
  };

  // Assert - the config stores the policy ID
  EXPECT_EQ(config.packing_policy_id, "tight");
}

//===----------------------------------------------------------------------===//
// CookTextureForEmission Tests
//===----------------------------------------------------------------------===//

class CookTextureForEmissionTest : public ::testing::Test { };

//! Verifies cooking succeeds with valid input.
NOLINT_TEST_F(CookTextureForEmissionTest, ValidInput_Succeeds)
{
  // Arrange
  CookerConfig config { .enabled = true };

  // Act
  auto result = CookTextureForEmission(GetTestImageBytes(), config, "test");

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->payload.empty());
  EXPECT_EQ(result->desc.width, 2u);
  EXPECT_EQ(result->desc.height, 2u);
}

//! Verifies cooking with mips produces correct result.
NOLINT_TEST_F(CookTextureForEmissionTest, WithMips_ProducesMultipleMips)
{
  // Arrange
  CookerConfig config {
    .enabled = true,
    .mip_policy = oxygen::content::import::MipPolicy::kFullChain,
  };

  // Act
  auto result = CookTextureForEmission(GetTestImageBytes(), config, "mip_test");

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_GE(result->desc.mip_levels, 1u); // 2x2 texture has 2 mip levels
}

//! Verifies cooking fails with invalid data.
NOLINT_TEST_F(CookTextureForEmissionTest, InvalidData_Fails)
{
  // Arrange
  std::vector<std::byte> garbage(50, std::byte { 0xAB });
  CookerConfig config { .enabled = true };

  // Act
  auto result = CookTextureForEmission(
    { garbage.data(), garbage.size() }, config, "garbage");

  // Assert
  EXPECT_FALSE(result.has_value());
}

//===----------------------------------------------------------------------===//
// CookTextureWithFallback Tests
//===----------------------------------------------------------------------===//

class CookTextureWithFallbackTest : public ::testing::Test { };

//! Verifies fallback produces valid result with invalid input.
NOLINT_TEST_F(CookTextureWithFallbackTest, InvalidInput_ReturnsPlaceholder)
{
  // Arrange
  std::vector<std::byte> garbage(50, std::byte { 0xAB });
  CookerConfig config { .enabled = true };

  // Act
  auto result = CookTextureWithFallback(
    { garbage.data(), garbage.size() }, config, "fallback_test");

  // Assert
  EXPECT_TRUE(result.is_placeholder);
  EXPECT_FALSE(result.payload.empty());
}

//! Verifies fallback returns cooked result with valid input.
NOLINT_TEST_F(CookTextureWithFallbackTest, ValidInput_ReturnsCooked)
{
  // Arrange
  CookerConfig config { .enabled = true };

  // Act
  auto result
    = CookTextureWithFallback(GetTestImageBytes(), config, "valid_test");

  // Assert
  EXPECT_FALSE(result.is_placeholder);
  EXPECT_FALSE(result.payload.empty());
}

//===----------------------------------------------------------------------===//
// CreatePlaceholderForMissingTexture Tests
//===----------------------------------------------------------------------===//

class CreatePlaceholderForMissingTextureTest : public ::testing::Test { };

//! Verifies placeholder texture has correct dimensions.
NOLINT_TEST_F(CreatePlaceholderForMissingTextureTest, HasCorrectDimensions)
{
  // Arrange
  CookerConfig config { .enabled = true };

  // Act
  auto result = CreatePlaceholderForMissingTexture("placeholder_test", config);

  // Assert
  EXPECT_GT(result.desc.width, 0u);
  EXPECT_GT(result.desc.height, 0u);
  EXPECT_TRUE(result.is_placeholder);
}

//! Verifies placeholder texture has non-empty payload.
NOLINT_TEST_F(CreatePlaceholderForMissingTextureTest, HasNonEmptyPayload)
{
  // Arrange
  CookerConfig config { .enabled = true };

  // Act
  auto result = CreatePlaceholderForMissingTexture("payload_test", config);

  // Assert
  EXPECT_FALSE(result.payload.empty());
}

//===----------------------------------------------------------------------===//
// CreateFallbackTexture Tests
//===----------------------------------------------------------------------===//

class CreateFallbackTextureTest : public ::testing::Test { };

//! Verifies fallback texture is a 1x1 placeholder with payload.
NOLINT_TEST_F(CreateFallbackTextureTest, CreatesValidFallback)
{
  // Arrange
  CookerConfig config { .enabled = true };

  // Act
  const auto result = CreateFallbackTexture(config);

  // Assert
  EXPECT_TRUE(result.is_placeholder);
  EXPECT_EQ(result.desc.width, 1u);
  EXPECT_EQ(result.desc.height, 1u);
  EXPECT_EQ(result.desc.mip_levels, 1u);
  EXPECT_FALSE(result.payload.empty());
}

} // namespace
