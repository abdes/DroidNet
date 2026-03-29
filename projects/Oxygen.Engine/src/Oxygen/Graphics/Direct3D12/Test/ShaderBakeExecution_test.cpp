//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Execution.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FinalArchivePack.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::ShaderDefine;
using oxygen::graphics::ShaderLibraryReader;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::BuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::ClearCache;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeFileFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::ComputeShaderActionKey;
using oxygen::graphics::d3d12::tools::shader_bake::DependencyFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::EnsureBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::ExecuteRebuild;
using oxygen::graphics::d3d12::tools::shader_bake::ExecuteUpdate;
using oxygen::graphics::d3d12::tools::shader_bake::ExecutionOptions;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandedShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::GetBuildRootLayout;
using oxygen::graphics::d3d12::tools::shader_bake::GetModuleArtifactPath;
using oxygen::graphics::d3d12::tools::shader_bake::GetRequestDxilPath;
using oxygen::graphics::d3d12::tools::shader_bake::GetRequestLogPath;
using oxygen::graphics::d3d12::tools::shader_bake::GetRequestPdbPath;
using oxygen::graphics::d3d12::tools::shader_bake::
  IsExternalShaderDebugInfoEnabled;
using oxygen::graphics::d3d12::tools::shader_bake::PackFinalShaderArchive;
using oxygen::graphics::d3d12::tools::shader_bake::ReadBuildStateFile;
using oxygen::graphics::d3d12::tools::shader_bake::ReadManifestFile;
using oxygen::graphics::d3d12::tools::shader_bake::RequestCompileOutcome;
using oxygen::graphics::d3d12::tools::shader_bake::RequestKeyToHex;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderBakeMode;

auto ReadFileBytes(const std::filesystem::path& path) -> std::vector<std::byte>
{
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open());
  const std::vector<char> file_bytes {
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>(),
  };
  std::vector<std::byte> out;
  out.reserve(file_bytes.size());
  for (const auto byte : file_bytes) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
  }
  return out;
}

auto MakePayload(uint64_t first, uint64_t second) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(sizeof(uint64_t) * 2U);
  std::memcpy(bytes.data(), &first, sizeof(uint64_t));
  std::memcpy(bytes.data() + sizeof(uint64_t), &second, sizeof(uint64_t));
  return bytes;
}

class FakeCompiler final {
public:
  FakeCompiler(std::filesystem::path workspace_root,
    std::filesystem::path shader_root,
    std::vector<std::filesystem::path> include_dirs, uint64_t toolchain_hash)
    : workspace_root_(std::move(workspace_root))
    , shader_root_(std::move(shader_root))
    , include_dirs_(std::move(include_dirs))
    , toolchain_hash_(toolchain_hash)
  {
  }

  auto SetDependencies(uint64_t request_key,
    std::vector<std::filesystem::path> dependencies) -> void
  {
    dependencies_[request_key] = std::move(dependencies);
  }

  auto FailRequest(uint64_t request_key, std::string message) -> void
  {
    failure_messages_[request_key] = std::move(message);
  }

  [[nodiscard]] auto CompileCount(uint64_t request_key) const -> size_t
  {
    if (const auto it = compile_counts_.find(request_key);
      it != compile_counts_.end()) {
      return it->second;
    }
    return 0;
  }

  auto operator()(const ExpandedShaderRequest& request, size_t /*index*/,
    size_t /*total_count*/) -> RequestCompileOutcome
  {
    ++compile_counts_[request.request_key];

    if (const auto it = failure_messages_.find(request.request_key);
      it != failure_messages_.end()) {
      return RequestCompileOutcome {
        .diagnostics = "request: " + request.request.source_path + ":"
          + request.request.entry_point + "\nerror: " + it->second + "\n",
      };
    }

    const auto source_file = shader_root_ / request.request.source_path;
    const auto primary = ComputeFileFingerprint(source_file, workspace_root_);

    uint64_t dependency_seed = 0;
    std::vector<DependencyFingerprint> dependencies;
    if (const auto it = dependencies_.find(request.request_key);
      it != dependencies_.end()) {
      dependencies.reserve(it->second.size());
      for (const auto& dependency_path : it->second) {
        auto fingerprint
          = ComputeFileFingerprint(dependency_path, workspace_root_);
        dependency_seed ^= fingerprint.content_hash;
        dependencies.push_back(std::move(fingerprint));
      }
    }

    return RequestCompileOutcome {
      .artifact
      = oxygen::graphics::d3d12::tools::shader_bake::ModuleArtifact {
        .request_key = request.request_key,
        .action_key = ComputeShaderActionKey(request.request, include_dirs_),
        .toolchain_hash = toolchain_hash_,
        .request = request.request,
        .primary_hash = primary.content_hash,
        .dependencies = std::move(dependencies),
        .dxil = MakePayload(request.request_key, primary.content_hash),
        .reflection = MakePayload(dependency_seed, toolchain_hash_),
      },
      .pdb = MakePayload(request.request_key, dependency_seed),
    };
  }

private:
  std::filesystem::path workspace_root_;
  std::filesystem::path shader_root_;
  std::vector<std::filesystem::path> include_dirs_;
  uint64_t toolchain_hash_ { 0 };
  std::unordered_map<uint64_t, std::vector<std::filesystem::path>>
    dependencies_;
  std::unordered_map<uint64_t, std::string> failure_messages_;
  std::unordered_map<uint64_t, size_t> compile_counts_;
};

class ShaderBakeExecutionTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto next_id = std::atomic_uint64_t { 0 };
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen_shaderbake_execution_test_"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(root_);

    workspace_root_ = root_ / "workspace";
    shader_root_ = workspace_root_ / "src" / "Oxygen" / "Graphics"
      / "Direct3D12" / "Shaders";
    build_root_ = root_ / "build";
    out_file_ = root_ / "bin" / "shaders.bin";
    include_dirs_ = { shader_root_ };
    layout_ = GetBuildRootLayout(build_root_);

    std::filesystem::create_directories(shader_root_);
    EnsureBuildRootLayout(layout_);
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

  auto MakeOptions(std::span<const ExpandedShaderRequest> requests,
    FakeCompiler& compiler, const ShaderBakeMode mode = ShaderBakeMode::kDev,
    const std::filesystem::path& out_file_override = {}) const
    -> ExecutionOptions
  {
    return ExecutionOptions {
      .mode = mode,
      .workspace_root = workspace_root_,
      .shader_source_root = shader_root_,
      .final_archive_path
      = out_file_override.empty() ? out_file_ : out_file_override,
      .layout = layout_,
      .toolchain_hash = toolchain_hash_,
      .include_dirs = include_dirs_,
      .requests = requests,
      .compile_request
      = [&compiler](const ExpandedShaderRequest& request, size_t index,
          size_t total_count) { return compiler(request, index, total_count); },
    };
  }

  [[nodiscard]] auto ReadArchiveModuleCount(
    const std::filesystem::path& archive_path) const -> size_t
  {
    return ShaderLibraryReader::ReadFromFile(archive_path).modules.size();
  }

  [[nodiscard]] auto ReadArchiveModulePaths(
    const std::filesystem::path& archive_path) const -> std::vector<std::string>
  {
    auto archive = ShaderLibraryReader::ReadFromFile(archive_path);
    std::vector<std::string> paths;
    paths.reserve(archive.modules.size());
    for (const auto& module : archive.modules) {
      paths.push_back(module.source_path + ":" + module.entry_point);
    }
    return paths;
  }

  const uint64_t toolchain_hash_ = 0x1234567890ABCDEFULL;
  std::filesystem::path root_;
  std::filesystem::path workspace_root_;
  std::filesystem::path shader_root_;
  std::filesystem::path build_root_;
  std::filesystem::path out_file_;
  std::vector<std::filesystem::path> include_dirs_;
  BuildRootLayout layout_;
};

} // namespace

NOLINT_TEST_F(ShaderBakeExecutionTest,
  PublishedDebugArtifactsUseEntryPointInFilenameInsteadOfDirectory)
{
  const auto request = MakeExpandedRequest(
    "Atmosphere/MultiScatLut_CS.hlsl", "CS", ShaderType::kCompute);
  const auto key_hex = RequestKeyToHex(request.request_key);

  const auto dxil_path
    = GetRequestDxilPath(out_file_, request.request.source_path,
      request.request.entry_point, request.request_key);
  const auto expected_dxil = out_file_.parent_path() / "dxil" / "Atmosphere"
    / "MultiScatLut_CS.hlsl" / ("CS__" + key_hex + ".dxil");

  EXPECT_EQ(dxil_path, expected_dxil);
  EXPECT_EQ(dxil_path.parent_path().filename().generic_string(),
    "MultiScatLut_CS.hlsl");
  EXPECT_EQ(dxil_path.filename().generic_string(), "CS__" + key_hex + ".dxil");

  const auto pdb_path
    = GetRequestPdbPath(out_file_, request.request.source_path,
      request.request.entry_point, request.request_key);
  const auto expected_pdb = out_file_.parent_path() / "pdb" / "Atmosphere"
    / "MultiScatLut_CS.hlsl" / ("CS__" + key_hex + ".pdb");
  EXPECT_EQ(pdb_path, expected_pdb);
}

NOLINT_TEST_F(ShaderBakeExecutionTest, UpdateRecompilesOnlyEditedLeafRequest)
{
  const auto first
    = MakeExpandedRequest("Leaf/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Leaf/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);

  const auto first_run = ExecuteUpdate(MakeOptions(requests, compiler));
  ASSERT_TRUE(first_run.has_value());
  EXPECT_EQ(first_run->compiled_request_count, 2U);
  EXPECT_EQ(first_run->clean_request_count, 0U);
  EXPECT_TRUE(std::filesystem::exists(GetRequestDxilPath(out_file_,
    first.request.source_path, first.request.entry_point, first.request_key)));
  EXPECT_TRUE(std::filesystem::exists(
    GetRequestDxilPath(out_file_, second.request.source_path,
      second.request.entry_point, second.request_key)));
  if (IsExternalShaderDebugInfoEnabled()) {
    EXPECT_TRUE(std::filesystem::exists(
      GetRequestPdbPath(out_file_, first.request.source_path,
        first.request.entry_point, first.request_key)));
    EXPECT_TRUE(std::filesystem::exists(
      GetRequestPdbPath(out_file_, second.request.source_path,
        second.request.entry_point, second.request_key)));
  }

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 3; }\n");

  const auto second_run = ExecuteUpdate(MakeOptions(requests, compiler));
  ASSERT_TRUE(second_run.has_value());
  EXPECT_EQ(second_run->compiled_request_count, 1U);
  EXPECT_EQ(second_run->clean_request_count, 1U);
  EXPECT_EQ(compiler.CompileCount(first.request_key), 2U);
  EXPECT_EQ(compiler.CompileCount(second.request_key), 1U);
  EXPECT_EQ(ReadArchiveModuleCount(out_file_), 2U);
}

NOLINT_TEST_F(
  ShaderBakeExecutionTest, UpdateRecompilesWhenExpectedPdbWasRemoved)
{
  if (!IsExternalShaderDebugInfoEnabled()) {
    GTEST_SKIP() << "External shader PDBs are only expected in debug builds";
  }

  const auto request
    = MakeExpandedRequest("Debug/Single_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { request };

  WriteTextFile(shader_root_ / request.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  ASSERT_TRUE(ExecuteUpdate(MakeOptions(requests, compiler)).has_value());

  const auto pdb_path
    = GetRequestPdbPath(out_file_, request.request.source_path,
      request.request.entry_point, request.request_key);
  ASSERT_TRUE(std::filesystem::exists(pdb_path));
  std::filesystem::remove(pdb_path);

  const auto second_run = ExecuteUpdate(MakeOptions(requests, compiler));
  ASSERT_TRUE(second_run.has_value());
  EXPECT_EQ(second_run->compiled_request_count, 1U);
  EXPECT_EQ(second_run->clean_request_count, 0U);
  EXPECT_EQ(compiler.CompileCount(request.request_key), 2U);
  EXPECT_TRUE(std::filesystem::exists(pdb_path));
}

NOLINT_TEST_F(ShaderBakeExecutionTest,
  UpdateRecompilesOnlyRequestsThatResolvedChangedInclude)
{
  const auto first
    = MakeExpandedRequest("Shared/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Shared/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const auto third
    = MakeExpandedRequest("Shared/Third_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second, third };

  const auto shared_include = shader_root_ / "Shared" / "Lighting.hlsli";
  const auto unrelated_include = shader_root_ / "Shared" / "Fog.hlsli";
  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");
  WriteTextFile(shader_root_ / third.request.source_path,
    "float4 PS() : SV_Target { return 3; }\n");
  WriteTextFile(shared_include, "float3 EvalLight() { return 1; }\n");
  WriteTextFile(unrelated_include, "float3 EvalFog() { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  compiler.SetDependencies(first.request_key, { shared_include });
  compiler.SetDependencies(second.request_key, { shared_include });
  compiler.SetDependencies(third.request_key, { unrelated_include });

  ASSERT_TRUE(ExecuteUpdate(MakeOptions(requests, compiler)).has_value());

  WriteTextFile(shared_include, "float3 EvalLight() { return 7; }\n");

  const auto second_run = ExecuteUpdate(MakeOptions(requests, compiler));
  ASSERT_TRUE(second_run.has_value());
  EXPECT_EQ(second_run->compiled_request_count, 2U);
  EXPECT_EQ(second_run->clean_request_count, 1U);
  EXPECT_EQ(compiler.CompileCount(first.request_key), 2U);
  EXPECT_EQ(compiler.CompileCount(second.request_key), 2U);
  EXPECT_EQ(compiler.CompileCount(third.request_key), 1U);
}

NOLINT_TEST_F(
  ShaderBakeExecutionTest, UpdateRemovesStaleArtifactsAndFinalArchiveMembership)
{
  const auto first
    = MakeExpandedRequest("Catalog/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Catalog/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array initial_requests { first, second };
  const std::array reduced_requests { first };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  ASSERT_TRUE(
    ExecuteUpdate(MakeOptions(initial_requests, compiler)).has_value());
  const auto stale_artifact_path
    = GetModuleArtifactPath(layout_, second.request_key);
  ASSERT_TRUE(std::filesystem::exists(stale_artifact_path));

  const auto second_run
    = ExecuteUpdate(MakeOptions(reduced_requests, compiler));
  ASSERT_TRUE(second_run.has_value());
  EXPECT_EQ(second_run->compiled_request_count, 0U);
  EXPECT_EQ(second_run->clean_request_count, 1U);
  EXPECT_EQ(second_run->stale_request_count, 1U);
  EXPECT_FALSE(std::filesystem::exists(stale_artifact_path));
  EXPECT_EQ(ReadArchiveModuleCount(out_file_), 1U);
  EXPECT_FALSE(std::filesystem::exists(
    GetRequestDxilPath(out_file_, second.request.source_path,
      second.request.entry_point, second.request_key)));
  if (IsExternalShaderDebugInfoEnabled()) {
    EXPECT_FALSE(std::filesystem::exists(
      GetRequestPdbPath(out_file_, second.request.source_path,
        second.request.entry_point, second.request_key)));
  }

  const auto manifest = ReadManifestFile(layout_.manifest_file);
  EXPECT_EQ(manifest.requests.size(), 1U);
  const auto build_state = ReadBuildStateFile(layout_.build_state_file);
  EXPECT_EQ(build_state.modules.size(), 1U);
}

NOLINT_TEST_F(
  ShaderBakeExecutionTest, UpdateAndRebuildProduceByteIdenticalArchives)
{
  const auto first
    = MakeExpandedRequest("Parity/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Parity/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler update_compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  ASSERT_TRUE(
    ExecuteUpdate(MakeOptions(requests, update_compiler)).has_value());
  const auto update_bytes = ReadFileBytes(out_file_);

  const auto rebuild_layout = GetBuildRootLayout(root_ / "rebuild");
  const auto rebuild_out = root_ / "bin" / "rebuild-shaders.bin";
  EnsureBuildRootLayout(rebuild_layout);
  FakeCompiler rebuild_compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  const ExecutionOptions rebuild_options {
    .mode = ShaderBakeMode::kDev,
    .workspace_root = workspace_root_,
    .shader_source_root = shader_root_,
    .final_archive_path = rebuild_out,
    .layout = rebuild_layout,
    .toolchain_hash = toolchain_hash_,
    .include_dirs = include_dirs_,
    .requests = requests,
    .compile_request =
      [&rebuild_compiler](const ExpandedShaderRequest& request, size_t index,
        size_t total_count) {
        return rebuild_compiler(request, index, total_count);
      },
  };

  ASSERT_TRUE(ExecuteRebuild(rebuild_options).has_value());
  const auto rebuild_bytes = ReadFileBytes(rebuild_out);
  EXPECT_EQ(update_bytes, rebuild_bytes);
}

NOLINT_TEST_F(ShaderBakeExecutionTest,
  UpdateFailurePreservesLastGoodArchiveAndArtifactsAndWritesDevLogs)
{
  const auto first
    = MakeExpandedRequest("Failure/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Failure/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  ASSERT_TRUE(ExecuteUpdate(MakeOptions(requests, compiler)).has_value());

  const auto archive_before = ReadFileBytes(out_file_);
  const auto first_artifact_path
    = GetModuleArtifactPath(layout_, first.request_key);
  const auto second_artifact_path
    = GetModuleArtifactPath(layout_, second.request_key);
  const auto first_artifact_before = ReadFileBytes(first_artifact_path);
  const auto second_artifact_before = ReadFileBytes(second_artifact_path);

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 8; }\n");
  compiler.FailRequest(first.request_key, "synthetic update failure");

  const auto failed_run = ExecuteUpdate(MakeOptions(requests, compiler));
  EXPECT_FALSE(failed_run.has_value());
  EXPECT_EQ(ReadFileBytes(out_file_), archive_before);
  EXPECT_EQ(ReadFileBytes(first_artifact_path), first_artifact_before);
  EXPECT_EQ(ReadFileBytes(second_artifact_path), second_artifact_before);
  EXPECT_TRUE(
    std::filesystem::exists(GetRequestLogPath(layout_, first.request_key)));
}

NOLINT_TEST_F(ShaderBakeExecutionTest,
  RebuildFailurePreservesArchiveAndMayLeaveSuccessfulArtifacts)
{
  const auto first
    = MakeExpandedRequest("Rebuild/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Rebuild/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  ASSERT_TRUE(ExecuteUpdate(MakeOptions(requests, compiler)).has_value());

  const auto archive_before = ReadFileBytes(out_file_);
  const auto first_artifact_path
    = GetModuleArtifactPath(layout_, first.request_key);
  const auto first_artifact_before = ReadFileBytes(first_artifact_path);

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 11; }\n");
  compiler.FailRequest(second.request_key, "synthetic rebuild failure");

  const auto failed_run = ExecuteRebuild(MakeOptions(requests, compiler));
  EXPECT_FALSE(failed_run.has_value());
  EXPECT_EQ(ReadFileBytes(out_file_), archive_before);
  EXPECT_NE(ReadFileBytes(first_artifact_path), first_artifact_before);
  EXPECT_TRUE(
    std::filesystem::exists(GetRequestLogPath(layout_, second.request_key)));
}

NOLINT_TEST_F(ShaderBakeExecutionTest, ProductionModeDoesNotWriteFailureLogs)
{
  const auto request
    = MakeExpandedRequest("Prod/Failure_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { request };

  WriteTextFile(shader_root_ / request.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  compiler.FailRequest(request.request_key, "synthetic production failure");

  const auto failed_run = ExecuteUpdate(
    MakeOptions(requests, compiler, ShaderBakeMode::kProduction));
  EXPECT_FALSE(failed_run.has_value());
  EXPECT_FALSE(
    std::filesystem::exists(GetRequestLogPath(layout_, request.request_key)));
}

NOLINT_TEST_F(
  ShaderBakeExecutionTest, ClearCacheRemovesShaderBakeIntermediatesOnly)
{
  WriteTextFile(layout_.build_state_file, "{}\n");
  WriteTextFile(layout_.manifest_file, "{}\n");
  WriteTextFile(layout_.modules_dir / "aa" / "bb" / "deadbeef.oxsm", "module");
  WriteTextFile(
    out_file_.parent_path() / "dxil" / "Foo.hlsl" / "PS" / "deadbeef.dxil",
    "dxil");
  WriteTextFile(
    out_file_.parent_path() / "pdb" / "Foo.hlsl" / "PS" / "deadbeef.pdb",
    "pdb");
  WriteTextFile(layout_.logs_dir / "deadbeef.log", "log");
  WriteTextFile(layout_.temp_dir / "scratch.tmp", "temp");
  WriteTextFile(out_file_, "final");
  const auto out_before = ReadFileBytes(out_file_);

  ClearCache(layout_);

  EXPECT_TRUE(std::filesystem::exists(layout_.root));
  EXPECT_FALSE(std::filesystem::exists(layout_.state_dir));
  EXPECT_FALSE(std::filesystem::exists(layout_.modules_dir));
  EXPECT_FALSE(std::filesystem::exists(layout_.logs_dir));
  EXPECT_FALSE(std::filesystem::exists(layout_.temp_dir));
  EXPECT_EQ(ReadFileBytes(out_file_), out_before);
  EXPECT_TRUE(std::filesystem::exists(
    out_file_.parent_path() / "dxil" / "Foo.hlsl" / "PS" / "deadbeef.dxil"));
  EXPECT_TRUE(std::filesystem::exists(
    out_file_.parent_path() / "pdb" / "Foo.hlsl" / "PS" / "deadbeef.pdb"));
}

NOLINT_TEST_F(ShaderBakeExecutionTest,
  FinalPackPreservesCatalogOrderAndRejectsMissingArtifacts)
{
  const auto first
    = MakeExpandedRequest("Pack/First_PS.hlsl", "PS", ShaderType::kPixel);
  const auto second
    = MakeExpandedRequest("Pack/Second_PS.hlsl", "PS", ShaderType::kPixel);
  const std::array requests { first, second };

  WriteTextFile(shader_root_ / first.request.source_path,
    "float4 PS() : SV_Target { return 1; }\n");
  WriteTextFile(shader_root_ / second.request.source_path,
    "float4 PS() : SV_Target { return 2; }\n");

  FakeCompiler compiler(
    workspace_root_, shader_root_, include_dirs_, toolchain_hash_);
  const auto first_artifact = compiler(first, 1, 2).artifact;
  const auto second_artifact = compiler(second, 2, 2).artifact;
  ASSERT_TRUE(first_artifact.has_value());
  ASSERT_TRUE(second_artifact.has_value());
  const std::vector<std::string> expected_paths {
    "Pack/First_PS.hlsl:PS",
    "Pack/Second_PS.hlsl:PS",
  };

  PackFinalShaderArchive(out_file_, toolchain_hash_, requests,
    std::array { *second_artifact, *first_artifact });
  EXPECT_EQ(ReadArchiveModulePaths(out_file_), expected_paths);

  EXPECT_THROW(PackFinalShaderArchive(out_file_, toolchain_hash_, requests,
                 std::array { *first_artifact }),
    std::runtime_error);
}
