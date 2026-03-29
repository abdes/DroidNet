//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FinalArchivePack.h>

#include <array>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  constexpr std::array<char, 8> kBackendString = {
    'd',
    '3',
    'd',
    '1',
    '2',
    '\0',
    '\0',
    '\0',
  };

} // namespace

auto PackFinalShaderArchive(const std::filesystem::path& out_file,
  const uint64_t toolchain_hash,
  const std::span<const ExpandedShaderRequest> requests,
  const std::span<const ModuleArtifact> artifacts) -> void
{
  if (requests.size() != artifacts.size()) {
    throw std::runtime_error(fmt::format(
      "final archive pack requires {} module artifacts, but {} were provided",
      requests.size(), artifacts.size()));
  }

  std::unordered_set<uint64_t> request_keys;
  request_keys.reserve(requests.size());
  for (const auto& request : requests) {
    if (!request_keys.insert(request.request_key).second) {
      throw std::runtime_error(fmt::format(
        "duplicate request key encountered during final pack: {:016x}",
        request.request_key));
    }
  }

  std::unordered_map<uint64_t, const ModuleArtifact*> artifacts_by_key;
  artifacts_by_key.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    if (!artifacts_by_key.emplace(artifact.request_key, &artifact).second) {
      throw std::runtime_error(fmt::format(
        "duplicate module artifact encountered during final pack: {:016x}",
        artifact.request_key));
    }
  }

  std::vector<oxygen::graphics::ShaderLibraryWriter::ModuleView> views;
  views.reserve(requests.size());
  for (const auto& request : requests) {
    const auto it = artifacts_by_key.find(request.request_key);
    if (it == artifacts_by_key.end()) {
      throw std::runtime_error(
        fmt::format("missing module artifact for request key {:016x}",
          request.request_key));
    }

    const auto& artifact = *it->second;
    if (artifact.request != request.request) {
      throw std::runtime_error(
        fmt::format("module artifact request mismatch for request key {:016x}",
          request.request_key));
    }

    views.push_back(oxygen::graphics::ShaderLibraryWriter::ModuleView {
      .stage = artifact.request.stage,
      .source_path = artifact.request.source_path,
      .entry_point = artifact.request.entry_point,
      .defines = std::span<const ShaderDefine>(artifact.request.defines),
      .dxil = std::span<const std::byte>(artifact.dxil),
      .reflection = std::span<const std::byte>(artifact.reflection),
    });
  }

  const oxygen::graphics::ShaderLibraryWriter writer(
    kBackendString, toolchain_hash);
  writer.WriteToFile(out_file, views);

  LOG_F(INFO, "Wrote {} modules to {}", views.size(), out_file.string());
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
