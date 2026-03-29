//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Bake.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DirtyAnalysis.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::ShaderDefine;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::AnalyzeDirtyRequests;
using oxygen::graphics::d3d12::tools::shader_bake::BuildBuildStateSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::BuildManifestSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::BuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeFileFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeShaderActionKey;
using oxygen::graphics::d3d12::tools::shader_bake::DependencyFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::DirtyAnalysisResult;
using oxygen::graphics::d3d12::tools::shader_bake::DirtyReason;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandedShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::GetBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetModuleArtifactPath;
using oxygen::graphics::d3d12::tools::shader_bake::GetRequestPdbPath;
using oxygen::graphics::d3d12::tools::shader_bake::
  IsExternalShaderDebugInfoEnabled;
using oxygen::graphics::d3d12::tools::shader_bake::ManifestSnapshot;
using oxygen::graphics::d3d12::tools::shader_bake::ModuleArtifact;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderBakeMode;
using oxygen::graphics::d3d12::tools::shader_bake::WriteBuildStateFile;
using oxygen::graphics::d3d12::tools::shader_bake::WriteManifestFile;
using oxygen::graphics::d3d12::tools::shader_bake::WriteModuleArtifactFile;

class ShaderBakeDirtyAnalysisTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto next_id = std::atomic_uint64_t { 0 };
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen_shaderbake_dirty_analysis_test_"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(root_);

    workspace_root_ = root_ / "workspace";
    shader_root_ = workspace_root_ / "src" / "Oxygen" / "Graphics"
      / "Direct3D12" / "Shaders";
    build_root_ = root_ / "build";
    out_file_ = root_ / "bin" / "shaders.bin";

    std::filesystem::create_directories(shader_root_);
    std::filesystem::create_directories(build_root_);
    layout_ = GetBuildRootLayout(build_root_);
    std::filesystem::create_directories(layout_.state_dir);
    std::filesystem::create_directories(layout_.modules_dir);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  auto WriteTextFile(const std::filesystem::path& path, std::string_view text)
    -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
  }

  auto MakeExpandedRequest(std::string_view source_path,
    std::string_view entry_point, ShaderType stage,
    std::vector<ShaderDefine> defines = {}) -> ExpandedShaderRequest
  {
    ShaderRequest request {
      .stage = stage,
      .source_path = std::string(source_path),
      .entry_point = std::string(entry_point),
      .defines = std::move(defines),
    };
    return ExpandedShaderRequest {
      .request = request,
      .request_key = ComputeShaderRequestKey(request),
    };
  }

  auto MakeArtifact(const ExpandedShaderRequest& request,
    std::span<const std::filesystem::path> include_dirs,
    std::span<const std::filesystem::path> dependency_paths) -> ModuleArtifact
  {
    const auto source_file = shader_root_ / request.request.source_path;
    const auto primary_hash
      = ComputeFileFingerprint(source_file, workspace_root_).content_hash;

    std::vector<DependencyFingerprint> dependencies;
    dependencies.reserve(dependency_paths.size());
    for (const auto& dependency : dependency_paths) {
      dependencies.push_back(
        ComputeFileFingerprint(dependency, workspace_root_));
    }

    return ModuleArtifact {
      .request_key = request.request_key,
      .action_key = ComputeShaderActionKey(request.request, include_dirs),
      .toolchain_hash = 0x1234,
      .request = request.request,
      .primary_hash = primary_hash,
      .dependencies = std::move(dependencies),
      .dxil = { std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 },
        std::byte { 0x04 } },
      .reflection = { std::byte { 0x05 } },
    };
  }

  auto PersistState(std::span<const ExpandedShaderRequest> requests,
    std::span<const ModuleArtifact> artifacts) -> void
  {
    WriteManifestFile(layout_.manifest_file, BuildManifestSnapshot(requests));
    WriteBuildStateFile(layout_.build_state_file,
      BuildBuildStateSnapshot(workspace_root_, layout_, artifacts));
  }

  auto WriteExpectedPdb(const ExpandedShaderRequest& request) -> void
  {
    if (!IsExternalShaderDebugInfoEnabled()) {
      return;
    }

    WriteTextFile(GetRequestPdbPath(out_file_, request.request.source_path,
                    request.request.entry_point, request.request_key),
      "pdb");
  }

  [[nodiscard]] static auto HasReason(
    const DirtyAnalysisResult& result, const DirtyReason reason) -> bool
  {
    EXPECT_EQ(result.requests.size(), 1U);
    const auto& request = result.requests.front();
    return std::find(
             request.dirty_reasons.begin(), request.dirty_reasons.end(), reason)
      != request.dirty_reasons.end();
  }

  std::filesystem::path root_;
  std::filesystem::path workspace_root_;
  std::filesystem::path shader_root_;
  std::filesystem::path build_root_;
  std::filesystem::path out_file_;
  BuildRootLayout layout_;
};

} // namespace

NOLINT_TEST_F(ShaderBakeDirtyAnalysisTest, ReusesCleanArtifactWhenInputsMatch)
{
  const auto request = MakeExpandedRequest(
    "Forward/ForwardMesh_PS.hlsl", "PS", ShaderType::kPixel);
  const auto source_file = shader_root_ / request.request.source_path;
  const auto dependency_file = shader_root_ / "Common" / "Lighting.hlsli";

  WriteTextFile(source_file, "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(dependency_file, "float3 EvaluateLighting() { return 1; }\n");

  const std::array include_dirs { shader_root_ };
  const auto artifact
    = MakeArtifact(request, include_dirs, std::array { dependency_file });
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, request.request_key), artifact);
  PersistState(std::array { request }, std::array { artifact });
  WriteExpectedPdb(request);
  WriteTextFile(out_file_, "existing");

  const auto analysis = AnalyzeDirtyRequests(workspace_root_, shader_root_,
    layout_, out_file_, std::array { request }, include_dirs);

  ASSERT_EQ(analysis.requests.size(), 1U);
  EXPECT_FALSE(analysis.requests.front().IsDirty());
  ASSERT_TRUE(analysis.requests.front().reusable_artifact.has_value());
  EXPECT_EQ(*analysis.requests.front().reusable_artifact, artifact);
  EXPECT_TRUE(analysis.stale_artifact_paths.empty());
  EXPECT_FALSE(analysis.manifest_changed);
  EXPECT_FALSE(analysis.final_archive_missing);
}

NOLINT_TEST_F(
  ShaderBakeDirtyAnalysisTest, MarksRequestDirtyWhenDependencyChanges)
{
  const auto request = MakeExpandedRequest(
    "Forward/ForwardMesh_PS.hlsl", "PS", ShaderType::kPixel);
  const auto source_file = shader_root_ / request.request.source_path;
  const auto dependency_file = shader_root_ / "Common" / "Lighting.hlsli";

  WriteTextFile(source_file, "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(dependency_file, "float3 EvaluateLighting() { return 1; }\n");

  const std::array include_dirs { shader_root_ };
  const auto artifact
    = MakeArtifact(request, include_dirs, std::array { dependency_file });
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, request.request_key), artifact);
  PersistState(std::array { request }, std::array { artifact });
  WriteTextFile(out_file_, "existing");

  WriteTextFile(
    dependency_file, "float3 EvaluateLighting() { return float3(2, 2, 2); }\n");

  const auto analysis = AnalyzeDirtyRequests(workspace_root_, shader_root_,
    layout_, out_file_, std::array { request }, include_dirs);

  EXPECT_TRUE(HasReason(analysis, DirtyReason::kDependencyChanged));
}

NOLINT_TEST_F(
  ShaderBakeDirtyAnalysisTest, MarksRequestDirtyWhenManifestAddsRequest)
{
  const auto request = MakeExpandedRequest(
    "Forward/ForwardMesh_PS.hlsl", "PS", ShaderType::kPixel);
  const auto source_file = shader_root_ / request.request.source_path;
  WriteTextFile(source_file, "float4 PS() : SV_Target { return 1; }\n");

  const std::array include_dirs { shader_root_ };
  const auto artifact = MakeArtifact(
    request, include_dirs, std::span<const std::filesystem::path> {});
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, request.request_key), artifact);
  WriteBuildStateFile(layout_.build_state_file,
    BuildBuildStateSnapshot(workspace_root_, layout_, std::array { artifact }));
  WriteManifestFile(layout_.manifest_file, ManifestSnapshot {});
  WriteTextFile(out_file_, "existing");

  const auto analysis = AnalyzeDirtyRequests(workspace_root_, shader_root_,
    layout_, out_file_, std::array { request }, include_dirs);

  EXPECT_TRUE(HasReason(analysis, DirtyReason::kNewManifestMembership));
  EXPECT_TRUE(analysis.manifest_changed);
}

NOLINT_TEST_F(
  ShaderBakeDirtyAnalysisTest, ReportsStaleArtifactsFromPriorBuildState)
{
  const auto current_request = MakeExpandedRequest(
    "Forward/ForwardMesh_PS.hlsl", "PS", ShaderType::kPixel);
  const auto stale_request
    = MakeExpandedRequest("Ui/ImGui.hlsl", "PS", ShaderType::kPixel);

  const auto current_source
    = shader_root_ / current_request.request.source_path;
  const auto stale_source = shader_root_ / stale_request.request.source_path;
  WriteTextFile(current_source, "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(stale_source, "float4 PS() : SV_Target { return 0; }\n");

  const std::array include_dirs { shader_root_ };
  const auto current_artifact = MakeArtifact(
    current_request, include_dirs, std::span<const std::filesystem::path> {});
  const auto stale_artifact = MakeArtifact(
    stale_request, include_dirs, std::span<const std::filesystem::path> {});

  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, current_request.request_key),
    current_artifact);
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, stale_request.request_key), stale_artifact);

  const std::array all_artifacts { current_artifact, stale_artifact };
  WriteBuildStateFile(layout_.build_state_file,
    BuildBuildStateSnapshot(workspace_root_, layout_, all_artifacts));
  WriteManifestFile(layout_.manifest_file,
    BuildManifestSnapshot(std::array { current_request }));
  WriteTextFile(out_file_, "existing");

  const auto analysis = AnalyzeDirtyRequests(workspace_root_, shader_root_,
    layout_, out_file_, std::array { current_request }, include_dirs);

  ASSERT_EQ(analysis.stale_artifact_paths.size(), 1U);
  EXPECT_EQ(analysis.stale_artifact_paths.front(),
    GetModuleArtifactPath(layout_, stale_request.request_key));
}

NOLINT_TEST_F(
  ShaderBakeDirtyAnalysisTest, MarksRequestDirtyWhenExpectedPdbIsMissing)
{
  if (!IsExternalShaderDebugInfoEnabled()) {
    GTEST_SKIP() << "External shader PDBs are only expected in debug builds";
  }

  const auto request = MakeExpandedRequest(
    "Forward/ForwardMesh_PS.hlsl", "PS", ShaderType::kPixel);
  const auto source_file = shader_root_ / request.request.source_path;
  WriteTextFile(source_file, "float4 PS() : SV_Target { return 1; }\n");

  const std::array include_dirs { shader_root_ };
  const auto artifact = MakeArtifact(
    request, include_dirs, std::span<const std::filesystem::path> {});
  WriteModuleArtifactFile(
    GetModuleArtifactPath(layout_, request.request_key), artifact);
  PersistState(std::array { request }, std::array { artifact });
  WriteTextFile(out_file_, "existing");

  const auto analysis = AnalyzeDirtyRequests(workspace_root_, shader_root_,
    layout_, out_file_, std::array { request }, include_dirs);

  EXPECT_TRUE(HasReason(analysis, DirtyReason::kMissingDebugArtifact));

  const auto pdb_path
    = GetRequestPdbPath(out_file_, request.request.source_path,
      request.request.entry_point, request.request_key);
  EXPECT_FALSE(std::filesystem::exists(pdb_path));
}
