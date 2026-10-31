//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::d3d12::kMaxDefinesPerShader;
using oxygen::graphics::d3d12::ShaderEntry;
using oxygen::graphics::d3d12::tools::shader_bake::BuildManifestSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::EnsureBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandedShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandShaderCatalog;
using oxygen::graphics::d3d12::tools::shader_bake::GetBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::ReadManifestFile;
using oxygen::graphics::d3d12::tools::shader_bake::WriteManifestFile;

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

class ShaderBakeManifestTest : public oxygen::testing::TempDirTest { };

} // namespace

NOLINT_TEST_F(ShaderBakeManifestTest, RoundTripsExpandedRequests)
{
  const std::array entries {
    MakeEntry(ShaderType::kPixel, "Forward/Debug.hlsl", "PS",
      { "DEBUG_CLUSTER_INDEX", "OXYGEN_HDR_OUTPUT" }),
    MakeEntry(ShaderType::kVertex, "Forward/Mesh.hlsl", "VS", {}),
  };

  const auto expanded = ExpandShaderCatalog(entries);
  const auto snapshot = BuildManifestSnapshot(expanded);
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  WriteManifestFile(layout.manifest_file, snapshot);
  const auto reloaded = ReadManifestFile(layout.manifest_file);

  EXPECT_EQ(reloaded, snapshot);
}

NOLINT_TEST_F(ShaderBakeManifestTest, RewritePublishesNewSnapshot)
{
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  const auto first = BuildManifestSnapshot(std::array {
    ExpandedShaderRequest {
      .request = {
        .stage = ShaderType::kVertex,
        .source_path = "A.hlsl",
        .entry_point = "VS",
      },
      .request_key = 0x1111111111111111ULL,
    },
  });
  WriteManifestFile(layout.manifest_file, first);

  const auto second = BuildManifestSnapshot(std::array {
    ExpandedShaderRequest {
      .request = {
        .stage = ShaderType::kPixel,
        .source_path = "B.hlsl",
        .entry_point = "PS",
      },
      .request_key = 0x2222222222222222ULL,
    },
  });
  WriteManifestFile(layout.manifest_file, second);

  const auto reloaded = ReadManifestFile(layout.manifest_file);
  EXPECT_EQ(reloaded, second);
  EXPECT_FALSE(std::filesystem::exists(layout.manifest_file.string() + ".tmp"));
}

NOLINT_TEST_F(ShaderBakeManifestTest, RejectsCorruptManifestJson)
{
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  {
    std::ofstream out(layout.manifest_file, std::ios::binary | std::ios::trunc);
    out << "{ not-json";
  }

  EXPECT_THROW(static_cast<void>(ReadManifestFile(layout.manifest_file)),
    std::runtime_error);
}
