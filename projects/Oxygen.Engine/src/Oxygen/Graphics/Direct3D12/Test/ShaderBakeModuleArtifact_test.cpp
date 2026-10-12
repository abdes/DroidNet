//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::CanonicalizeShaderRequest;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::ShaderDefine;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::d3d12::ShaderViewBinding;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeShaderActionKey;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeToolchainHash;
using oxygen::graphics::d3d12::tools::shader_bake::DependencyFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::EnsureBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetModuleArtifactPath;
using oxygen::graphics::d3d12::tools::shader_bake::MakeShaderCompileProfile;
using oxygen::graphics::d3d12::tools::shader_bake::ModuleArtifact;
using oxygen::graphics::d3d12::tools::shader_bake::ReadModuleArtifactFile;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderCompileProfile;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderConfig;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderDebugInfo;
using oxygen::graphics::d3d12::tools::shader_bake::TryReadModuleArtifactFile;
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

auto MakeCanonicalRequest() -> ShaderRequest
{
  return CanonicalizeShaderRequest(ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Forward/Debug.hlsl",
    .entry_point = "PSMain",
    .defines = {
      ShaderDefine { .name = "A_FEATURE", .value = std::nullopt },
      ShaderDefine { .name = "Z_MODE", .value = std::string("1") },
    },
  });
}

class ShaderBakeModuleArtifactTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto next_id = std::atomic_uint64_t { 0 };
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen_shaderbake_module_artifact_test_"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] auto BuildSampleArtifact() const -> ModuleArtifact
  {
    const auto request = MakeCanonicalRequest();
    return ModuleArtifact {
      .request_key = ComputeShaderRequestKey(request),
      .action_key = ComputeShaderActionKey(request, ShaderViewBinding::kNone,
        std::array {
          std::filesystem::path("H:/oxygen/shaders"),
          std::filesystem::path("H:/oxygen/includes"),
        },
        MakeShaderCompileProfile(ShaderConfig::kDebug)),
      .toolchain_hash
      = ComputeToolchainHash(MakeShaderCompileProfile(ShaderConfig::kDebug)),
      .request = request,
      .primary_hash = 0xFEDCBA9876543210ULL,
      .dependencies = {
        DependencyFingerprint {
          .path = "src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/Lighting.hlsli",
          .content_hash = 0x0102030405060708ULL,
          .size_bytes = 128,
          .write_time_utc = 123456789,
        },
        DependencyFingerprint {
          .path = "src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/Math.hlsli",
          .content_hash = 0x1112131415161718ULL,
          .size_bytes = 512,
          .write_time_utc = 987654321,
        },
      },
      .dxil = MakeBytes({ 0x44, 0x58, 0x49, 0x4C }),
      .reflection = MakeBytes({ 0x10, 0x20, 0x30, 0x40, 0x50 }),
    };
  }

  std::filesystem::path root_;
};

} // namespace

NOLINT_TEST_F(ShaderBakeModuleArtifactTest, ComputesShardedModuleArtifactPath)
{
  const auto layout = GetBuildRootLayout(root_ / "build");
  const auto artifact_path
    = GetModuleArtifactPath(layout, 0x1234567890ABCDEFULL);

  EXPECT_EQ(
    artifact_path, layout.modules_dir / "12" / "34" / "1234567890abcdef.oxsm");
}

NOLINT_TEST_F(ShaderBakeModuleArtifactTest, RoundTripsModuleArtifact)
{
  const auto layout = GetBuildRootLayout(root_ / "build");
  EnsureBuildRootLayout(layout);

  const auto artifact = BuildSampleArtifact();
  const auto artifact_path
    = GetModuleArtifactPath(layout, artifact.request_key);

  WriteModuleArtifactFile(artifact_path, artifact);
  const auto reloaded = ReadModuleArtifactFile(artifact_path);

  EXPECT_EQ(reloaded, artifact);
  EXPECT_FALSE(std::filesystem::exists(artifact_path.string() + ".tmp"));
}

NOLINT_TEST_F(ShaderBakeModuleArtifactTest, CorruptArtifactBecomesCacheMiss)
{
  const auto layout = GetBuildRootLayout(root_ / "build");
  EnsureBuildRootLayout(layout);

  const auto artifact_path
    = GetModuleArtifactPath(layout, 0x1111222233334444ULL);
  std::filesystem::create_directories(artifact_path.parent_path());
  {
    std::ofstream out(artifact_path, std::ios::binary);
    out << "bad";
  }

  EXPECT_FALSE(TryReadModuleArtifactFile(artifact_path).has_value());
}

NOLINT_TEST(ShaderBakeActionKeyTest, ChangesWhenIncludeRootOrderChanges)
{
  const auto request = MakeCanonicalRequest();
  const std::array first_order {
    std::filesystem::path("H:/oxygen/shaders"),
    std::filesystem::path("H:/oxygen/includes"),
  };
  const std::array second_order {
    std::filesystem::path("H:/oxygen/includes"),
    std::filesystem::path("H:/oxygen/shaders"),
  };

  const auto profile = MakeShaderCompileProfile(ShaderConfig::kRelease);
  constexpr auto kNone = ShaderViewBinding::kNone;
  const auto first
    = ComputeShaderActionKey(request, kNone, first_order, profile);
  const auto repeated
    = ComputeShaderActionKey(request, kNone, first_order, profile);
  const auto second
    = ComputeShaderActionKey(request, kNone, second_order, profile);

  EXPECT_EQ(first, repeated);
  EXPECT_NE(first, second);
}

//! The camera-view define changes the compiled module without changing the
//! request, so it must change the action key.
NOLINT_TEST(ShaderBakeActionKeyTest, ChangesWhenViewBindingChanges)
{
  const auto request = MakeCanonicalRequest();
  const std::array include_dirs { std::filesystem::path("H:/oxygen/shaders") };
  const auto profile = MakeShaderCompileProfile(ShaderConfig::kDebug);

  EXPECT_NE(ComputeShaderActionKey(
              request, ShaderViewBinding::kNone, include_dirs, profile),
    ComputeShaderActionKey(
      request, ShaderViewBinding::kCamera, include_dirs, profile));
}

NOLINT_TEST(ShaderBakeActionKeyTest, EveryCompileProfileHasItsOwnKeys)
{
  const auto request = MakeCanonicalRequest();
  const std::array include_dirs { std::filesystem::path("H:/oxygen/shaders") };
  std::vector<ShaderCompileProfile> profiles;
  for (const auto config : { ShaderConfig::kDebug, ShaderConfig::kRelease }) {
    for (const auto debug_info : {
           ShaderDebugInfo::kNone,
           ShaderDebugInfo::kExternal,
           ShaderDebugInfo::kEmbedded,
         }) {
      profiles.push_back(MakeShaderCompileProfile(config, debug_info));
    }
  }

  for (size_t i = 0; i < profiles.size(); ++i) {
    for (size_t j = i + 1; j < profiles.size(); ++j) {
      EXPECT_NE(ComputeShaderActionKey(request, ShaderViewBinding::kNone,
                  include_dirs, profiles.at(i)),
        ComputeShaderActionKey(
          request, ShaderViewBinding::kNone, include_dirs, profiles.at(j)))
        << i << " vs " << j;
      EXPECT_NE(ComputeToolchainHash(profiles.at(i)),
        ComputeToolchainHash(profiles.at(j)))
        << i << " vs " << j;
    }
  }
}
