//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/TrackingIncludeHandler.h>

namespace {

using oxygen::graphics::d3d12::tools::shader_bake::ComputeFileFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::DependencyFingerprint;
using oxygen::graphics::d3d12::tools::shader_bake::ResolveTrackedInclude;
using oxygen::graphics::d3d12::tools::shader_bake::TrackingDependencyRecorder;

class ShaderBakeDependenciesTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto next_id = std::atomic_uint64_t { 0 };
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen_shaderbake_dependencies_test_"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
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

  std::filesystem::path root_;
};

} // namespace

NOLINT_TEST_F(
  ShaderBakeDependenciesTest, ResolveTrackedIncludeUsesOrderedSearchRoots)
{
  const auto workspace_root = root_ / "workspace";
  const auto first_root = workspace_root / "includes-first";
  const auto second_root = workspace_root / "includes-second";
  const auto candidate_rel = std::filesystem::path("Vortex/Shared/Math.hlsli");
  const auto first_file = first_root / candidate_rel;
  const auto second_file = second_root / candidate_rel;
  WriteTextFile(first_file, "first");
  WriteTextFile(second_file, "second");

  const auto resolved = ResolveTrackedInclude(L"Vortex/Shared/Math.hlsli",
    workspace_root, std::array { first_root, second_root });

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->absolute_path, first_file.lexically_normal());
  EXPECT_EQ(resolved->fingerprint.path, "includes-first/Vortex/Shared/Math.hlsli");
}

NOLINT_TEST_F(ShaderBakeDependenciesTest,
  ComputeFileFingerprintCanonicalizesWorkspaceRelativePath)
{
  const auto workspace_root = root_ / "workspace";
  const auto file_path
    = workspace_root / "src" / "Shaders" / "Common" / "Lighting.hlsli";
  WriteTextFile(file_path, "float4 main() : SV_Target { return 1; }\n");

  const auto fingerprint = ComputeFileFingerprint(file_path, workspace_root);

  EXPECT_EQ(fingerprint.path, "src/Shaders/Vortex/Shared/Lighting.hlsli");
  EXPECT_EQ(fingerprint.size_bytes, std::filesystem::file_size(file_path));
  EXPECT_NE(fingerprint.content_hash, 0U);
}

NOLINT_TEST(
  ShaderBakeDependenciesRecorderTest, DeduplicatesPreservingFirstEncounterOrder)
{
  TrackingDependencyRecorder recorder;
  recorder.RecordDependency(DependencyFingerprint {
    .path = "Shaders/Vortex/Shared/Math.hlsli",
    .content_hash = 1,
    .size_bytes = 10,
    .write_time_utc = 20,
  });
  recorder.RecordDependency(DependencyFingerprint {
    .path = "Shaders/Vortex/Shared/Math.hlsli",
    .content_hash = 99,
    .size_bytes = 999,
    .write_time_utc = 999,
  });
  recorder.RecordDependency(DependencyFingerprint {
    .path = "Shaders/Vortex/Shared/Lighting.hlsli",
    .content_hash = 2,
    .size_bytes = 20,
    .write_time_utc = 30,
  });

  ASSERT_EQ(recorder.Dependencies().size(), 2U);
  EXPECT_EQ(recorder.Dependencies()[0].path, "Shaders/Vortex/Shared/Math.hlsli");
  EXPECT_EQ(recorder.Dependencies()[1].path, "Shaders/Vortex/Shared/Lighting.hlsli");
}

NOLINT_TEST_F(ShaderBakeDependenciesTest, ResolveTrackedIncludeFailsWhenMissing)
{
  const auto workspace_root = root_ / "workspace";
  std::filesystem::create_directories(workspace_root / "includes");

  const auto resolved = ResolveTrackedInclude(L"Missing/Nope.hlsli",
    workspace_root, std::array { workspace_root / "includes" });

  EXPECT_FALSE(resolved.has_value());
}
