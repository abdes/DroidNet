//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Types/MaterialPermutations.h>

#include <regex>
#include <string>

#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::engine::permutation;
using oxygen::ShaderType;
using oxygen::graphics::CanonicalizeShaderRequest;
using oxygen::graphics::ShaderDefine;
using oxygen::graphics::ShaderRequest;

namespace {

//! Helper to validate a define name matches shader system requirements.
//! Valid names: SCREAMING_SNAKE_CASE, starting with letter.
auto IsValidDefineName(std::string_view name) -> bool
{
  static const std::regex pattern { "^[A-Z][A-Z0-9_]*$" };
  return std::regex_match(std::string { name }, pattern);
}

//! All permutation constants must be valid shader define names.
NOLINT_TEST(MaterialPermutationsTest, AllDefineNames_AreValid)
{
  EXPECT_TRUE(IsValidDefineName(kAlphaTest))
    << "kAlphaTest is not a valid define name: " << kAlphaTest;

  EXPECT_TRUE(IsValidDefineName(kDoubleSided))
    << "kDoubleSided is not a valid define name: " << kDoubleSided;

  EXPECT_TRUE(IsValidDefineName(kHasEmissive))
    << "kHasEmissive is not a valid define name: " << kHasEmissive;

  EXPECT_TRUE(IsValidDefineName(kHasClearcoat))
    << "kHasClearcoat is not a valid define name: " << kHasClearcoat;

  EXPECT_TRUE(IsValidDefineName(kHasTransmission))
    << "kHasTransmission is not a valid define name: " << kHasTransmission;

  EXPECT_TRUE(IsValidDefineName(kHasHeightMap))
    << "kHasHeightMap is not a valid define name: " << kHasHeightMap;
}

//! Permutation constants can be used in ShaderRequest without canonicalization
//! errors.
NOLINT_TEST(MaterialPermutationsTest, DefineNames_PassCanonicalization)
{
  auto req = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = std::string { kAlphaTest }, .value = "1" },
    },
  };

  // Should not throw
  NOLINT_EXPECT_NO_THROW(static_cast<void>(CanonicalizeShaderRequest(req)));
}

//! All permutation constants can be combined in a single ShaderRequest.
NOLINT_TEST(MaterialPermutationsTest, AllDefines_CanBeCombined)
{
  auto req = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = std::string { kAlphaTest }, .value = "1" },
      ShaderDefine { .name = std::string { kHasEmissive }, .value = "1" },
      ShaderDefine { .name = std::string { kHasClearcoat }, .value = "1" },
    },
  };

  // Should not throw
  NOLINT_EXPECT_NO_THROW(static_cast<void>(CanonicalizeShaderRequest(req)));
}

} // namespace
