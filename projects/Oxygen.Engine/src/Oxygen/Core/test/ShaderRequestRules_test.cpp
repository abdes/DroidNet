//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <string>

#include <Oxygen/Core/Types/ShaderRequestRules.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::CanonicalShaderDefines;
using oxygen::CanonicalShaderSourcePath;
using oxygen::IsShaderDefineName;
using oxygen::IsShaderEntryPoint;
using oxygen::IsShaderStage;
using oxygen::ShaderType;

TEST(ShaderRequestRulesTest, EntryPointsAreIdentifiers)
{
  EXPECT_TRUE(IsShaderEntryPoint("PS"));
  EXPECT_TRUE(IsShaderEntryPoint("_main2"));
  EXPECT_FALSE(IsShaderEntryPoint(""));
  EXPECT_FALSE(IsShaderEntryPoint("2main"));
  EXPECT_FALSE(IsShaderEntryPoint("ma in"));
  EXPECT_FALSE(IsShaderEntryPoint("main.hlsl"));
}

TEST(ShaderRequestRulesTest, DefineNamesAreUpperCase)
{
  EXPECT_TRUE(IsShaderDefineName("ALPHA_TEST"));
  EXPECT_TRUE(IsShaderDefineName("LOD2"));
  EXPECT_FALSE(IsShaderDefineName(""));
  EXPECT_FALSE(IsShaderDefineName("_ALPHA"));
  EXPECT_FALSE(IsShaderDefineName("alpha"));
}

TEST(ShaderRequestRulesTest, CanonicalDefinesAreSortedByName)
{
  EXPECT_EQ(CanonicalShaderDefines(""), std::optional<std::string> { "" });
  EXPECT_EQ(CanonicalShaderDefines("ALPHA_TEST=1"),
    std::optional<std::string> { "ALPHA_TEST=1" });
  EXPECT_EQ(CanonicalShaderDefines("LOD=2,ALPHA_TEST,BLEND=add"),
    std::optional<std::string> { "ALPHA_TEST,BLEND=add,LOD=2" });
  // A value is everything after the first '=', and may be empty.
  EXPECT_EQ(CanonicalShaderDefines("B=x=y,A="),
    std::optional<std::string> { "A=,B=x=y" });
}

TEST(ShaderRequestRulesTest, StagesAreDefinedShaderTypes)
{
  EXPECT_TRUE(IsShaderStage(static_cast<uint32_t>(ShaderType::kVertex)));
  EXPECT_TRUE(IsShaderStage(static_cast<uint32_t>(ShaderType::kMaxShaderType)));
  EXPECT_FALSE(IsShaderStage(static_cast<uint32_t>(ShaderType::kUnknown)));
  EXPECT_FALSE(
    IsShaderStage(static_cast<uint32_t>(ShaderType::kMaxShaderType) + 1U));
}

TEST(ShaderRequestRulesTest, MalformedDefinesHaveNoCanonicalForm)
{
  for (const auto* defines :
    { ",", "A,", ",A", "a=1", "A=1 2", "A=1,A=2", "A,A", "A B" }) {
    SCOPED_TRACE(defines);
    EXPECT_EQ(CanonicalShaderDefines(defines), std::nullopt);
  }
}

TEST(ShaderRequestRulesTest, CanonicalPathsAreRelativeAndNormal)
{
  EXPECT_EQ(CanonicalShaderSourcePath("shaders/test.hlsl"),
    std::optional<std::string> { "shaders/test.hlsl" });
  EXPECT_EQ(CanonicalShaderSourcePath("./shaders\\common/../test.hlsl"),
    std::optional<std::string> { "shaders/test.hlsl" });
  EXPECT_EQ(CanonicalShaderSourcePath("shaders//./lit/"),
    std::optional<std::string> { "shaders/lit" });
}

TEST(ShaderRequestRulesTest, CanonicalPathsKeepUtf8Names)
{
  const auto path = std::string { "Shaders/\xC3\x89t\xC3\xA9.hlsl" };

  EXPECT_EQ(CanonicalShaderSourcePath(path), std::optional { path });
}

TEST(ShaderRequestRulesTest, PathsOutsideTheShadersDirectoryHaveNoCanonicalForm)
{
  EXPECT_EQ(CanonicalShaderSourcePath(""), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("."), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("C:/abs.hlsl"), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("shaders:bad.hlsl"), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("/abs.hlsl"), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("../up.hlsl"), std::nullopt);
  EXPECT_EQ(CanonicalShaderSourcePath("a/../../up.hlsl"), std::nullopt);
}

} // namespace
