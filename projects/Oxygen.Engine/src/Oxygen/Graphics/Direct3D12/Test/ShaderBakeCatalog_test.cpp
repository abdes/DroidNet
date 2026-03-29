//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::d3d12::kEngineShaders;
using oxygen::graphics::d3d12::kMaxDefinesPerShader;
using oxygen::graphics::d3d12::ShaderEntry;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandShaderCatalog;

auto MakeEntry(const ShaderType type, std::string_view path,
  std::string_view entry_point, std::initializer_list<std::string_view> defines)
  -> ShaderEntry
{
  ShaderEntry entry {
    .type = type,
    .path = path,
    .entry_point = entry_point,
  };

  size_t index = 0;
  for (const auto define : defines) {
    if (index >= kMaxDefinesPerShader) {
      throw std::invalid_argument("too many test defines");
    }
    entry.defines[index++] = define;
  }
  entry.define_count = index;

  return entry;
}

} // namespace

NOLINT_TEST(ShaderBakeCatalogTest, ExpandsCanonicalizesAndSortsRequests)
{
  const std::array entries {
    MakeEntry(ShaderType::kPixel, "Zeta\\Shader.hlsl", "PS", {}),
    MakeEntry(ShaderType::kPixel, "Alpha/Path.hlsl", "PS", { "BETA", "ALPHA" }),
    MakeEntry(ShaderType::kVertex, "Alpha/Path.hlsl", "VS", {}),
    MakeEntry(ShaderType::kPixel, "Alpha/Path.hlsl", "PS", { "ALPHA" }),
  };

  const auto expanded = ExpandShaderCatalog(entries);

  ASSERT_EQ(expanded.size(), 4u);

  EXPECT_EQ(expanded[0].request.source_path, "Alpha/Path.hlsl");
  EXPECT_EQ(expanded[0].request.stage, ShaderType::kVertex);
  EXPECT_TRUE(expanded[0].request.defines.empty());

  EXPECT_EQ(expanded[1].request.source_path, "Alpha/Path.hlsl");
  EXPECT_EQ(expanded[1].request.stage, ShaderType::kPixel);
  ASSERT_EQ(expanded[1].request.defines.size(), 1u);
  EXPECT_EQ(expanded[1].request.defines[0].name, "ALPHA");
  ASSERT_TRUE(expanded[1].request.defines[0].value.has_value());
  EXPECT_EQ(*expanded[1].request.defines[0].value, "1");

  EXPECT_EQ(expanded[2].request.source_path, "Alpha/Path.hlsl");
  EXPECT_EQ(expanded[2].request.stage, ShaderType::kPixel);
  ASSERT_EQ(expanded[2].request.defines.size(), 2u);
  EXPECT_EQ(expanded[2].request.defines[0].name, "ALPHA");
  EXPECT_EQ(expanded[2].request.defines[1].name, "BETA");

  EXPECT_EQ(expanded[3].request.source_path, "Zeta/Shader.hlsl");
  EXPECT_EQ(expanded[3].request.stage, ShaderType::kPixel);
  EXPECT_EQ(expanded[3].request.entry_point, "PS");

  for (const auto& request : expanded) {
    EXPECT_EQ(request.request_key, ComputeShaderRequestKey(request.request));
  }
}

NOLINT_TEST(ShaderBakeCatalogTest, RejectsDuplicateRequestKeys)
{
  const std::array entries {
    MakeEntry(ShaderType::kPixel, "Dup.hlsl", "PS", { "ALPHA" }),
    MakeEntry(ShaderType::kPixel, "Dup.hlsl", "PS", { "ALPHA" }),
  };

  EXPECT_THROW(
    static_cast<void>(ExpandShaderCatalog(entries)), std::runtime_error);
}

NOLINT_TEST(ShaderBakeCatalogTest, EngineCatalogHasUniqueCanonicalRequests)
{
  const auto expanded = ExpandShaderCatalog(kEngineShaders);

  EXPECT_EQ(expanded.size(), kEngineShaders.size());
}
