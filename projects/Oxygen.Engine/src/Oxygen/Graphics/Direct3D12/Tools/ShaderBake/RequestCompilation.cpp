//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/RequestCompilation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DxcShaderCompiler.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Reflect.h>

using oxygen::graphics::ShaderInfo;

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto ReadFileUtf8(const std::filesystem::path& file) -> std::u8string
  {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error(
        fmt::format("failed to open shader source file: {}", file.string()));
    }

    std::vector<char> bytes(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::u8string(reinterpret_cast<const char8_t*>(bytes.data()),
      reinterpret_cast<const char8_t*>(bytes.data()) + bytes.size());
  }

} // namespace

auto CompileExpandedShaderRequest(const RequestCompilerConfig& config,
  const ExpandedShaderRequest& expanded_request, const size_t index,
  const size_t total_count) -> RequestCompileOutcome
{
  const auto& request = expanded_request.request;
  const ShaderInfo shader {
    .type = request.stage,
    .relative_path = request.source_path,
    .entry_point = request.entry_point,
    .defines = request.defines,
  };

  LOG_F(INFO, "[{}/{}] {}", index, total_count, FormatShaderLogKey(request));

  try {
    const auto shader_file = config.shader_source_root / shader.relative_path;
    const auto source = ReadFileUtf8(shader_file);

    DxcShaderCompiler compiler(
      DxcShaderCompiler::Config { .name = "DXC", .global_defines = {} });
    const DxcShaderCompiler::CompileOptions shader_options {
      .workspace_root = config.workspace_root,
      .include_dirs = std::vector<std::filesystem::path>(
        config.include_dirs.begin(), config.include_dirs.end()),
      .defines = shader.defines,
      .object_output_name
      = RequestKeyToHex(expanded_request.request_key) + ".dxil",
      .debug_output_name = IsExternalShaderDebugInfoEnabled()
        ? RequestKeyToHex(expanded_request.request_key) + ".pdb"
        : std::string {},
    };

    auto compile_result
      = compiler.CompileFromSource(source, shader, shader_options);
    if (!compile_result.Succeeded()) {
      LOG_F(ERROR, "Failed to compile {}:{}", shader.relative_path,
        shader.entry_point);
      return RequestCompileOutcome {
        .diagnostics = compile_result.diagnostics,
      };
    }

    auto bytecode = std::move(compile_result.bytecode);
    const auto* dxil_words = bytecode->Data();
    const size_t dxil_size_bytes = bytecode->Size();
    if ((dxil_size_bytes % sizeof(uint32_t)) != 0U) {
      throw std::runtime_error("DXIL bytecode size is not 4-byte aligned");
    }

    const auto dxil_word_count = dxil_size_bytes / sizeof(uint32_t);
    const auto dxil_as_bytes
      = std::as_bytes(std::span(dxil_words, dxil_word_count));
    std::vector<std::byte> dxil(dxil_as_bytes.begin(), dxil_as_bytes.end());

    auto reflection
      = ExtractAndSerializeReflection(shader, std::span<const std::byte>(dxil));
    const auto primary_hash
      = oxygen::ComputeFNV1a64(source.data(), source.size());
    const auto action_key
      = ComputeShaderActionKey(request, config.include_dirs);

    LOG_F(INFO, "  dxil={} bytes, reflection={} bytes", dxil.size(),
      reflection.size());

    return RequestCompileOutcome {
      .artifact = ModuleArtifact {
        .request_key = expanded_request.request_key,
        .action_key = action_key,
        .toolchain_hash = config.toolchain_hash,
        .request = request,
        .primary_hash = primary_hash,
        .dependencies = std::move(compile_result.dependencies),
        .dxil = std::move(dxil),
        .reflection = std::move(reflection),
      },
      .pdb = std::move(compile_result.pdb),
    };
  } catch (const std::exception& ex) {
    const auto diagnostics = fmt::format(
      "request: {}\nerror: {}\n", FormatShaderLogKey(request), ex.what());
    LOG_F(ERROR, "Failed to compile {}: {}", FormatShaderLogKey(request),
      ex.what());
    return RequestCompileOutcome {
      .diagnostics = diagnostics,
    };
  }
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
