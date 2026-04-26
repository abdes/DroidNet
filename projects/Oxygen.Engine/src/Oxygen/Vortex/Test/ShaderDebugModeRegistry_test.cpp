//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>

#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>

namespace {

using oxygen::vortex::DiagnosticsDebugPath;
using oxygen::vortex::EnumerateShaderDebugModes;
using oxygen::vortex::FindShaderDebugModeInfo;
using oxygen::vortex::GetShaderDebugDefineName;
using oxygen::vortex::IsIblDebugMode;
using oxygen::vortex::IsLightCullingDebugMode;
using oxygen::vortex::IsNonIblDebugMode;
using oxygen::vortex::ResolveShaderDebugMode;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::ShaderDebugModeFamily;
using oxygen::vortex::UsesForwardMeshDebugVariant;

namespace d3d12 = oxygen::graphics::d3d12;

constexpr auto kAllShaderDebugModes = std::array {
  ShaderDebugMode::kDisabled,
  ShaderDebugMode::kLightCullingHeatMap,
  ShaderDebugMode::kDepthSlice,
  ShaderDebugMode::kClusterIndex,
  ShaderDebugMode::kIblSpecular,
  ShaderDebugMode::kIblRawSky,
  ShaderDebugMode::kIblIrradiance,
  ShaderDebugMode::kBaseColor,
  ShaderDebugMode::kWorldNormals,
  ShaderDebugMode::kRoughness,
  ShaderDebugMode::kMetalness,
  ShaderDebugMode::kIblFaceIndex,
  ShaderDebugMode::kIblNoBrdfLut,
  ShaderDebugMode::kDirectLightingOnly,
  ShaderDebugMode::kIblOnly,
  ShaderDebugMode::kDirectPlusIbl,
  ShaderDebugMode::kDirectLightingFull,
  ShaderDebugMode::kDirectLightGates,
  ShaderDebugMode::kDirectBrdfCore,
  ShaderDebugMode::kDirectionalShadowMask,
  ShaderDebugMode::kSceneDepthRaw,
  ShaderDebugMode::kSceneDepthLinear,
  ShaderDebugMode::kSceneDepthMismatch,
  ShaderDebugMode::kMaskedAlphaCoverage,
};

[[nodiscard]] auto CatalogContainsDefine(std::string_view define) -> bool
{
  for (const auto& shader : d3d12::kEngineShaders) {
    for (std::size_t i = 0; i < shader.define_count; ++i) {
      if (shader.defines[i] == define) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] auto CatalogContainsDefineForPath(
  std::string_view define, std::string_view path) -> bool
{
  for (const auto& shader : d3d12::kEngineShaders) {
    if (shader.path != path) {
      continue;
    }
    for (std::size_t i = 0; i < shader.define_count; ++i) {
      if (shader.defines[i] == define) {
        return true;
      }
    }
  }
  return false;
}

NOLINT_TEST(ShaderDebugModeRegistryTest, HasOneEntryForEveryEnumValue)
{
  const auto entries = EnumerateShaderDebugModes();
  EXPECT_EQ(entries.size(), kAllShaderDebugModes.size());

  for (const auto mode : kAllShaderDebugModes) {
    const auto* info = FindShaderDebugModeInfo(mode);
    ASSERT_NE(info, nullptr) << static_cast<int>(mode);
    EXPECT_EQ(info->mode, mode);
    EXPECT_FALSE(info->canonical_name.empty());
    EXPECT_FALSE(info->display_name.empty());
  }
}

NOLINT_TEST(ShaderDebugModeRegistryTest, CanonicalNamesAreUniqueAndResolvable)
{
  auto names = std::set<std::string> {};
  for (const auto& info : EnumerateShaderDebugModes()) {
    EXPECT_TRUE(names.insert(std::string(info.canonical_name)).second)
      << info.canonical_name;

    const auto resolved = ResolveShaderDebugMode(info.canonical_name);
    ASSERT_TRUE(resolved.has_value()) << info.canonical_name;
    EXPECT_EQ(*resolved, info.mode);
    EXPECT_EQ(FindShaderDebugModeInfo(info.canonical_name), &info);
  }

  EXPECT_FALSE(ResolveShaderDebugMode("not-a-debug-mode").has_value());
}

NOLINT_TEST(ShaderDebugModeRegistryTest, SupportedShaderDefinesExistInCatalog)
{
  for (const auto& info : EnumerateShaderDebugModes()) {
    if (!info.supported || info.shader_define.empty()) {
      continue;
    }

    EXPECT_TRUE(CatalogContainsDefine(info.shader_define))
      << info.canonical_name << " define=" << info.shader_define;
  }
}

NOLINT_TEST(ShaderDebugModeRegistryTest, DebugPathMatchesCatalogShaderFamily)
{
  for (const auto& info : EnumerateShaderDebugModes()) {
    if (!info.supported || info.shader_define.empty()) {
      continue;
    }

    switch (info.path) {
    case DiagnosticsDebugPath::kForwardMeshVariant:
      EXPECT_TRUE(CatalogContainsDefineForPath(info.shader_define,
        "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl")
        || CatalogContainsDefineForPath(info.shader_define,
          "Vortex/Stages/Translucency/ForwardDebug_PS.hlsl"))
        << info.canonical_name << " define=" << info.shader_define;
      break;
    case DiagnosticsDebugPath::kDeferredFullscreen:
      EXPECT_TRUE(CatalogContainsDefineForPath(info.shader_define,
        "Vortex/Stages/BasePass/BasePassDebugView.hlsl"))
        << info.canonical_name << " define=" << info.shader_define;
      break;
    case DiagnosticsDebugPath::kNone:
    case DiagnosticsDebugPath::kServicePass:
    case DiagnosticsDebugPath::kExternalToolOnly:
      ADD_FAILURE() << info.canonical_name
                    << " has a shader define but no catalog-backed debug path";
      break;
    }
  }
}

NOLINT_TEST(ShaderDebugModeRegistryTest, CanonicalToolNamesStayStable)
{
  EXPECT_EQ(FindShaderDebugModeInfo(ShaderDebugMode::kDirectionalShadowMask)
              ->canonical_name,
    "directional-shadow-mask");
  EXPECT_EQ(FindShaderDebugModeInfo(ShaderDebugMode::kSceneDepthLinear)
              ->canonical_name,
    "scene-depth-linear");
  EXPECT_EQ(FindShaderDebugModeInfo(ShaderDebugMode::kBaseColor)
              ->canonical_name,
    "base-color");
}

NOLINT_TEST(ShaderDebugModeRegistryTest, HelperClassificationMatchesRegistry)
{
  for (const auto& info : EnumerateShaderDebugModes()) {
    EXPECT_EQ(IsLightCullingDebugMode(info.mode),
      info.family == ShaderDebugModeFamily::kLightCulling)
      << info.canonical_name;
    EXPECT_EQ(IsIblDebugMode(info.mode),
      info.mode == ShaderDebugMode::kIblSpecular
        || info.mode == ShaderDebugMode::kIblRawSky
        || info.mode == ShaderDebugMode::kIblIrradiance
        || info.mode == ShaderDebugMode::kIblFaceIndex)
      << info.canonical_name;
    EXPECT_EQ(UsesForwardMeshDebugVariant(info.mode),
      info.mode == ShaderDebugMode::kDirectLightingOnly
        || info.mode == ShaderDebugMode::kIblOnly
        || info.mode == ShaderDebugMode::kDirectPlusIbl
        || info.mode == ShaderDebugMode::kDirectLightingFull
        || info.mode == ShaderDebugMode::kDirectLightGates
        || info.mode == ShaderDebugMode::kDirectBrdfCore)
      << info.canonical_name;

    const bool is_legacy_non_ibl
      = info.mode == ShaderDebugMode::kLightCullingHeatMap
      || info.mode == ShaderDebugMode::kDepthSlice
      || info.mode == ShaderDebugMode::kClusterIndex
      || info.mode == ShaderDebugMode::kBaseColor
      || info.mode == ShaderDebugMode::kWorldNormals
      || info.mode == ShaderDebugMode::kRoughness
      || info.mode == ShaderDebugMode::kMetalness
      || info.mode == ShaderDebugMode::kDirectLightingOnly
      || info.mode == ShaderDebugMode::kIblOnly
      || info.mode == ShaderDebugMode::kDirectPlusIbl
      || info.mode == ShaderDebugMode::kDirectLightingFull
      || info.mode == ShaderDebugMode::kDirectLightGates
      || info.mode == ShaderDebugMode::kDirectBrdfCore
      || info.mode == ShaderDebugMode::kDirectionalShadowMask
      || info.mode == ShaderDebugMode::kSceneDepthRaw
      || info.mode == ShaderDebugMode::kSceneDepthLinear
      || info.mode == ShaderDebugMode::kSceneDepthMismatch
      || info.mode == ShaderDebugMode::kMaskedAlphaCoverage;
    EXPECT_EQ(IsNonIblDebugMode(info.mode), is_legacy_non_ibl)
      << info.canonical_name;
  }
}

NOLINT_TEST(ShaderDebugModeRegistryTest, ShaderDefineNamesMatchLegacyHelper)
{
  for (const auto& info : EnumerateShaderDebugModes()) {
    if (!info.supported) {
      continue;
    }
    EXPECT_EQ(info.shader_define, GetShaderDebugDefineName(info.mode))
      << info.canonical_name;
  }
}

NOLINT_TEST(
  ShaderDebugModeRegistryTest, DocumentsKnownUnwiredIblNoBrdfLutMode)
{
  const auto* info = FindShaderDebugModeInfo(ShaderDebugMode::kIblNoBrdfLut);
  ASSERT_NE(info, nullptr);
  EXPECT_FALSE(info->supported);
  EXPECT_EQ(info->path, DiagnosticsDebugPath::kNone);
  EXPECT_FALSE(info->unsupported_reason.empty());
}

NOLINT_TEST(ShaderDebugModeRegistryTest, FamilyStringsAreStableForUiGroups)
{
  EXPECT_EQ(oxygen::vortex::to_string(ShaderDebugModeFamily::kLightCulling),
    "light-culling");
  EXPECT_EQ(oxygen::vortex::to_string(ShaderDebugModeFamily::kDirectLighting),
    "direct-lighting");
  EXPECT_EQ(oxygen::vortex::to_string(ShaderDebugModeFamily::kSceneDepth),
    "scene-depth");
}

} // namespace
