//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/RequestCompilation.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ShaderType;
using oxygen::graphics::CanonicalizeShaderRequest;
using oxygen::graphics::ComputeShaderRequestKey;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::d3d12::ShaderViewBinding;
using oxygen::graphics::d3d12::tools::shader_bake::CompileExpandedShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::ExpandedShaderRequest;
using oxygen::graphics::d3d12::tools::shader_bake::MakeShaderCompileProfile;
using oxygen::graphics::d3d12::tools::shader_bake::RequestCompileOutcome;
using oxygen::graphics::d3d12::tools::shader_bake::RequestCompilerConfig;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderConfig;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderDebugInfo;

constexpr std::string_view kBoundViewHeader
  = "Vortex/Contracts/View/ViewConstants.hlsli";

//! A header the compilation includes, copied from the include root that holds
//! it, as the bake searches the shader root and the engine source root.
struct IncludedHeader {
  std::string_view include_root;
  std::string_view path;
};

//! The bound-view header and every header it includes. Each must lie in the
//! test's own root, since the bake refuses includes outside its workspace.
constexpr auto kIncludedHeaders = std::to_array<IncludedHeader>({
  { .include_root = OXYGEN_ENGINE_SHADER_ROOT, .path = kBoundViewHeader },
  {
    .include_root = OXYGEN_ENGINE_SHADER_ROOT,
    .path = "Vortex/Contracts/View/Generated.ViewConstants.hlsli",
  },
  {
    .include_root = OXYGEN_ENGINE_SOURCE_ROOT,
    .path = "Core/Bindless/Generated.BindlessAbi.hlsl",
  },
});

// Reads one camera view constant, so it compiles only as a camera entry.
constexpr std::string_view kProbeSource = R"(
#include "Vortex/Contracts/View/ViewConstants.hlsli"

RWByteAddressBuffer probe_output : register(u0);

[numthreads(1, 1, 1)]
void ProbeCS()
{
    probe_output.Store(0, frame_slot);
}
)";

class ShaderBakeRequestCompilationTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto next_id = std::atomic_uint64_t { 0 };
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen_shaderbake_request_compilation_test_"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);

    // The engine's own bound-view header, so the test follows its guard.
    for (const auto& [include_root, path] : kIncludedHeaders) {
      const auto header = root_ / path;
      std::filesystem::create_directories(header.parent_path());
      std::filesystem::copy_file(
        std::filesystem::path(include_root) / path, header);
    }
    WriteTextFile(root_ / "Probe.hlsl", kProbeSource);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view text) -> void
  {
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  [[nodiscard]] auto Compile(const ShaderViewBinding view_binding) const
    -> RequestCompileOutcome
  {
    auto request = CanonicalizeShaderRequest(ShaderRequest {
      .stage = ShaderType::kCompute,
      .source_path = "Probe.hlsl",
      .entry_point = "ProbeCS",
    });
    const auto request_key = ComputeShaderRequestKey(request);
    const ExpandedShaderRequest expanded {
      .request = std::move(request),
      .request_key = request_key,
      .view_binding = view_binding,
    };
    const std::array include_dirs { root_ };
    const RequestCompilerConfig config {
      .workspace_root = root_,
      .shader_source_root = root_,
      .profile
      = MakeShaderCompileProfile(ShaderConfig::kDebug, ShaderDebugInfo::kNone),
      .include_dirs = include_dirs,
    };
    return CompileExpandedShaderRequest(config, expanded, 1, 1);
  }

private:
  std::filesystem::path root_;
};

} // namespace

//! A camera entry may read the camera view constants.
NOLINT_TEST_F(ShaderBakeRequestCompilationTest, CameraEntryReadsViewConstants)
{
  const auto outcome = Compile(ShaderViewBinding::kCamera);

  EXPECT_TRUE(outcome.artifact.has_value()) << outcome.diagnostics;
}

//! Any other entry that reaches the bound-view header fails to compile and
//! names the header's guard.
NOLINT_TEST_F(
  ShaderBakeRequestCompilationTest, NonCameraEntryRejectsBoundViewHeader)
{
  const auto outcome = Compile(ShaderViewBinding::kNone);

  EXPECT_FALSE(outcome.artifact.has_value());
  EXPECT_NE(outcome.diagnostics.find("camera view entry"), std::string::npos)
    << outcome.diagnostics;
}
