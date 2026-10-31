//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::CanonicalizeShaderRequest;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::ShaderDefine;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::BuildBuildStateSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::BuildStateSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::EnsureBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetModuleArtifactPath;
using oxygen::graphics::d3d12::tools::shader_bake::LoadOrRecoverBuildStateFile;
using oxygen::graphics::d3d12::tools::shader_bake::ModuleArtifact;
using oxygen::graphics::d3d12::tools::shader_bake::ReadBuildStateFile;
using oxygen::graphics::d3d12::tools::shader_bake::WriteBuildStateFile;
using oxygen::graphics::d3d12::tools::shader_bake::WriteModuleArtifactFile;

auto MakeBytes(std::initializer_list<uint8_t> values) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes;
  bytes.reserve(values.size());
  for (const auto value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

auto MakeArtifact(const ShaderType stage, std::string_view source_path,
  std::string_view entry_point, std::vector<ShaderDefine> defines,
  const uint64_t action_key) -> ModuleArtifact
{
  auto request = CanonicalizeShaderRequest(ShaderRequest {
    .stage = stage,
    .source_path = std::string(source_path),
    .entry_point = std::string(entry_point),
    .defines = std::move(defines),
  });

  return ModuleArtifact {
    .request_key = ComputeShaderRequestKey(request),
    .action_key = action_key,
    .toolchain_hash = 0xCAFEBABE12345678ULL,
    .request = request,
    .primary_hash = 0x0102030405060708ULL,
    .dependencies = {},
    .dxil = MakeBytes({ 0x44, 0x58, 0x49, 0x4C }),
    .reflection = MakeBytes({ 0xAA, 0xBB, 0xCC }),
  };
}

class ShaderBakeBuildStateTest : public oxygen::testing::TempDirTest { };

} // namespace

NOLINT_TEST_F(ShaderBakeBuildStateTest, RoundTripsBuildStateSnapshot)
{
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  const std::array artifacts {
    MakeArtifact(ShaderType::kPixel, "Forward/Debug.hlsl", "PSMain",
      { ShaderDefine { .name = "DEBUG_PASS", .value = std::nullopt } },
      0x1111111111111111ULL),
    MakeArtifact(ShaderType::kVertex, "Forward/Mesh.hlsl", "VSMain",
      { ShaderDefine { .name = "ALPHA_TEST", .value = std::string("1") } },
      0x2222222222222222ULL),
  };

  const auto snapshot = BuildBuildStateSnapshot(TempDir(), layout, artifacts);
  WriteBuildStateFile(layout.build_state_file, snapshot);

  const auto reloaded = ReadBuildStateFile(layout.build_state_file);
  EXPECT_EQ(reloaded, snapshot);
  EXPECT_FALSE(
    std::filesystem::exists(layout.build_state_file.string() + ".tmp"));
}

NOLINT_TEST_F(
  ShaderBakeBuildStateTest, RecoversFromMissingBuildStateByScanningModules)
{
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  const std::array artifacts {
    MakeArtifact(ShaderType::kCompute,
      "Vortex/Services/PostProcess/Exposure.hlsl", "VortexExposureFrameCS", {},
      0x3333333333333333ULL),
    MakeArtifact(ShaderType::kPixel, "Ui/ImGui.hlsl", "PS",
      { ShaderDefine { .name = "DEBUG_PASS", .value = std::nullopt } },
      0x4444444444444444ULL),
  };

  for (const auto& artifact : artifacts) {
    WriteModuleArtifactFile(
      GetModuleArtifactPath(layout, artifact.request_key), artifact);
  }

  const auto recovered = LoadOrRecoverBuildStateFile(TempDir(), layout);
  const auto expected = BuildBuildStateSnapshot(TempDir(), layout, artifacts);

  EXPECT_EQ(recovered, expected);
}

NOLINT_TEST_F(
  ShaderBakeBuildStateTest, RecoversFromCorruptBuildStateByScanningModules)
{
  const auto layout = GetBuildRootLayout(TempDir() / "build");
  EnsureBuildRootLayout(layout);

  const auto artifact = MakeArtifact(ShaderType::kVertex, "Renderer/Grid.hlsl",
    "VSMain", { ShaderDefine { .name = "DEBUG_PASS", .value = std::nullopt } },
    0x5555555555555555ULL);
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout, artifact.request_key), artifact);

  {
    std::ofstream out(
      layout.build_state_file, std::ios::binary | std::ios::trunc);
    out << "{ not-json";
  }

  const auto recovered = LoadOrRecoverBuildStateFile(TempDir(), layout);
  const auto expected
    = BuildBuildStateSnapshot(TempDir(), layout, std::span(&artifact, 1));

  EXPECT_EQ(recovered, expected);
}
