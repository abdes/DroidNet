//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  using nlohmann::json;

  constexpr uint32_t kBuildStateSchemaVersion = 1;
  constexpr uint32_t kShaderBakeStateVersion = 1;

  auto ParseHexU64(std::string_view text, std::string_view field_name)
    -> uint64_t
  {
    if (text.size() != 16) {
      throw std::runtime_error(
        std::string(field_name) + " must be 16 lowercase hex characters");
    }

    uint64_t value = 0;
    for (const char ch : text) {
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<uint64_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<uint64_t>(ch - 'a' + 10);
      } else {
        throw std::runtime_error(
          std::string(field_name) + " contains a non-hex character");
      }
    }

    return value;
  }

  auto ShaderStageToStateString(const ShaderType stage) -> std::string_view
  {
    switch (stage) {
    case ShaderType::kAmplification:
      return "amplification";
    case ShaderType::kMesh:
      return "mesh";
    case ShaderType::kVertex:
      return "vertex";
    case ShaderType::kHull:
      return "hull";
    case ShaderType::kDomain:
      return "domain";
    case ShaderType::kGeometry:
      return "geometry";
    case ShaderType::kPixel:
      return "pixel";
    case ShaderType::kCompute:
      return "compute";
    case ShaderType::kRayGen:
      return "raygen";
    case ShaderType::kIntersection:
      return "intersection";
    case ShaderType::kAnyHit:
      return "anyhit";
    case ShaderType::kClosestHit:
      return "closesthit";
    case ShaderType::kMiss:
      return "miss";
    case ShaderType::kCallable:
      return "callable";
    default:
      break;
    }

    throw std::runtime_error("unsupported shader stage in build-state");
  }

  auto StateStringToShaderStage(std::string_view stage) -> ShaderType
  {
    if (stage == "amplification") {
      return ShaderType::kAmplification;
    }
    if (stage == "mesh") {
      return ShaderType::kMesh;
    }
    if (stage == "vertex") {
      return ShaderType::kVertex;
    }
    if (stage == "hull") {
      return ShaderType::kHull;
    }
    if (stage == "domain") {
      return ShaderType::kDomain;
    }
    if (stage == "geometry") {
      return ShaderType::kGeometry;
    }
    if (stage == "pixel") {
      return ShaderType::kPixel;
    }
    if (stage == "compute") {
      return ShaderType::kCompute;
    }
    if (stage == "raygen") {
      return ShaderType::kRayGen;
    }
    if (stage == "intersection") {
      return ShaderType::kIntersection;
    }
    if (stage == "anyhit") {
      return ShaderType::kAnyHit;
    }
    if (stage == "closesthit") {
      return ShaderType::kClosestHit;
    }
    if (stage == "miss") {
      return ShaderType::kMiss;
    }
    if (stage == "callable") {
      return ShaderType::kCallable;
    }

    throw std::runtime_error("unsupported shader stage string in build-state");
  }

  auto SerializeDefine(const ShaderDefine& define) -> json
  {
    auto payload = json { { "name", define.name } };
    if (define.value.has_value()) {
      payload["value"] = *define.value;
    } else {
      payload["value"] = nullptr;
    }
    return payload;
  }

  auto SerializeDefines(std::span<const ShaderDefine> defines) -> json
  {
    auto result = json::array();
    for (const auto& define : defines) {
      result.push_back(SerializeDefine(define));
    }
    return result;
  }

  auto ParseDefines(const json& defines_json) -> std::vector<ShaderDefine>
  {
    if (!defines_json.is_array()) {
      throw std::runtime_error("defines must be an array");
    }

    std::vector<ShaderDefine> defines;
    defines.reserve(defines_json.size());
    for (const auto& define_json : defines_json) {
      ShaderDefine define {
        .name = define_json.at("name").get<std::string>(),
      };
      if (const auto& value_json = define_json.at("value");
        !value_json.is_null()) {
        define.value = value_json.get<std::string>();
      }
      defines.push_back(std::move(define));
    }
    return defines;
  }

  auto CompareDefines(
    std::span<const ShaderDefine> lhs, std::span<const ShaderDefine> rhs) -> int
  {
    const size_t common_size = std::min(lhs.size(), rhs.size());
    for (size_t index = 0; index < common_size; ++index) {
      if (lhs[index].name != rhs[index].name) {
        return lhs[index].name < rhs[index].name ? -1 : 1;
      }

      const auto lhs_value = lhs[index].value.value_or("");
      const auto rhs_value = rhs[index].value.value_or("");
      if (lhs_value != rhs_value) {
        return lhs_value < rhs_value ? -1 : 1;
      }
    }

    if (lhs.size() == rhs.size()) {
      return 0;
    }
    return lhs.size() < rhs.size() ? -1 : 1;
  }

  auto CompareBuildStateModules(const BuildStateModuleRecord& lhs,
    const BuildStateModuleRecord& rhs) -> bool
  {
    if (lhs.request.source_path != rhs.request.source_path) {
      return lhs.request.source_path < rhs.request.source_path;
    }

    const auto lhs_stage
      = static_cast<std::underlying_type_t<ShaderType>>(lhs.request.stage);
    const auto rhs_stage
      = static_cast<std::underlying_type_t<ShaderType>>(rhs.request.stage);
    if (lhs_stage != rhs_stage) {
      return lhs_stage < rhs_stage;
    }

    if (lhs.request.entry_point != rhs.request.entry_point) {
      return lhs.request.entry_point < rhs.request.entry_point;
    }

    const auto defines_compare
      = CompareDefines(lhs.request.defines, rhs.request.defines);
    if (defines_compare != 0) {
      return defines_compare < 0;
    }

    if (lhs.request_key != rhs.request_key) {
      return lhs.request_key < rhs.request_key;
    }

    return lhs.module_artifact_relpath.generic_string()
      < rhs.module_artifact_relpath.generic_string();
  }

  auto MakeModuleRecord(const BuildRootLayout& layout,
    const ModuleArtifact& artifact) -> BuildStateModuleRecord
  {
    return BuildStateModuleRecord {
      .request_key = artifact.request_key,
      .action_key = artifact.action_key,
      .request = artifact.request,
      .module_artifact_relpath
      = GetModuleArtifactPath(layout, artifact.request_key)
        .lexically_relative(layout.root),
    };
  }

  auto NormalizeRelativeArtifactPath(const std::filesystem::path& relpath)
    -> std::filesystem::path
  {
    if (relpath.empty()) {
      throw std::runtime_error("module_artifact_relpath must not be empty");
    }
    if (relpath.is_absolute()) {
      throw std::runtime_error("module_artifact_relpath must be relative");
    }

    const auto normalized = relpath.lexically_normal();
    if (normalized.empty() || normalized == ".") {
      throw std::runtime_error(
        "module_artifact_relpath must not normalize to empty");
    }
    for (const auto& part : normalized) {
      if (part == "..") {
        throw std::runtime_error(
          "module_artifact_relpath must not escape build root");
      }
    }
    return normalized;
  }

  auto ValidateSnapshotForWrite(const BuildStateSnapshot& state)
    -> BuildStateSnapshot
  {
    BuildStateSnapshot validated {
      .workspace_root = state.workspace_root.lexically_normal(),
      .build_root = state.build_root.lexically_normal(),
      .modules = state.modules,
    };

    for (auto& module : validated.modules) {
      module.request
        = CanonicalizeShaderRequest(ShaderRequest { module.request });
      if (ComputeShaderRequestKey(module.request) != module.request_key) {
        throw std::runtime_error(
          "build-state request key does not match request");
      }
      module.module_artifact_relpath
        = NormalizeRelativeArtifactPath(module.module_artifact_relpath);
    }

    std::sort(validated.modules.begin(), validated.modules.end(),
      CompareBuildStateModules);
    return validated;
  }

  auto SnapshotMatchesLayout(const BuildStateSnapshot& state,
    const std::filesystem::path& workspace_root, const BuildRootLayout& layout)
    -> bool
  {
    return state.workspace_root.lexically_normal()
      == workspace_root.lexically_normal()
      && state.build_root.lexically_normal() == layout.root.lexically_normal();
  }

} // namespace

auto BuildBuildStateSnapshot(const std::filesystem::path& workspace_root,
  const BuildRootLayout& layout, std::span<const ModuleArtifact> artifacts)
  -> BuildStateSnapshot
{
  BuildStateSnapshot state {
    .workspace_root = workspace_root.lexically_normal(),
    .build_root = layout.root.lexically_normal(),
  };
  state.modules.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    state.modules.push_back(MakeModuleRecord(layout, artifact));
  }

  std::sort(
    state.modules.begin(), state.modules.end(), CompareBuildStateModules);
  return state;
}

auto WriteBuildStateFile(const std::filesystem::path& build_state_path,
  const BuildStateSnapshot& state) -> void
{
  const auto validated = ValidateSnapshotForWrite(state);

  auto modules_json = json::array();
  for (const auto& module : validated.modules) {
    modules_json.push_back(json {
      { "request_key_hex", RequestKeyToHex(module.request_key) },
      { "action_key_hex", RequestKeyToHex(module.action_key) },
      { "source_path", module.request.source_path },
      { "entry_point", module.request.entry_point },
      { "stage", std::string(ShaderStageToStateString(module.request.stage)) },
      { "defines", SerializeDefines(module.request.defines) },
      { "module_artifact_relpath",
        module.module_artifact_relpath.generic_string() },
    });
  }

  auto payload = json {
    { "schema_version", kBuildStateSchemaVersion },
    { "shaderbake_version", kShaderBakeStateVersion },
    { "workspace_root", validated.workspace_root.generic_string() },
    { "build_root", validated.build_root.generic_string() },
    { "module_count", validated.modules.size() },
    { "modules", std::move(modules_json) },
  };

  auto serialized = payload.dump(2);
  serialized.push_back('\n');
  WriteTextFileAtomically(build_state_path, serialized);
}

auto ReadBuildStateFile(const std::filesystem::path& build_state_path)
  -> BuildStateSnapshot
{
  std::ifstream stream(build_state_path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open build-state `"
      + ToUtf8PathString(build_state_path) + "`");
  }

  try {
    const auto payload = json::parse(stream);
    if (payload.at("schema_version").get<uint32_t>()
      != kBuildStateSchemaVersion) {
      throw std::runtime_error("unsupported build-state schema version");
    }
    if (payload.at("shaderbake_version").get<uint32_t>()
      != kShaderBakeStateVersion) {
      throw std::runtime_error("unsupported shaderbake_version");
    }

    const auto& modules_json = payload.at("modules");
    if (!modules_json.is_array()) {
      throw std::runtime_error("build-state modules must be an array");
    }

    const auto module_count = payload.at("module_count").get<size_t>();
    if (module_count != modules_json.size()) {
      throw std::runtime_error(
        "build-state module_count does not match modules size");
    }

    BuildStateSnapshot state {
      .workspace_root
      = std::filesystem::path(payload.at("workspace_root").get<std::string>()),
      .build_root
      = std::filesystem::path(payload.at("build_root").get<std::string>()),
    };
    state.modules.reserve(modules_json.size());

    for (const auto& module_json : modules_json) {
      state.modules.push_back(BuildStateModuleRecord {
        .request_key = ParseHexU64(
          module_json.at("request_key_hex").get<std::string>(), "request_key_hex"),
        .action_key = ParseHexU64(
          module_json.at("action_key_hex").get<std::string>(), "action_key_hex"),
        .request = ShaderRequest {
          .stage = StateStringToShaderStage(
            module_json.at("stage").get<std::string>()),
          .source_path = module_json.at("source_path").get<std::string>(),
          .entry_point = module_json.at("entry_point").get<std::string>(),
          .defines = ParseDefines(module_json.at("defines")),
        },
        .module_artifact_relpath = NormalizeRelativeArtifactPath(
          std::filesystem::path(
            module_json.at("module_artifact_relpath").get<std::string>())),
      });
    }

    return ValidateSnapshotForWrite(state);
  } catch (const std::exception& e) {
    throw std::runtime_error("failed to read build-state `"
      + ToUtf8PathString(build_state_path) + "`: " + e.what());
  }
}

auto ScanModulesForBuildState(const std::filesystem::path& workspace_root,
  const BuildRootLayout& layout) -> BuildStateSnapshot
{
  BuildStateSnapshot state {
    .workspace_root = workspace_root.lexically_normal(),
    .build_root = layout.root.lexically_normal(),
  };

  std::error_code ec;
  if (!std::filesystem::exists(layout.modules_dir, ec) || ec) {
    return state;
  }

  for (std::filesystem::recursive_directory_iterator it(layout.modules_dir, ec),
    end;
    it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file() || it->path().extension() != ".oxsm") {
      continue;
    }

    const auto artifact = TryReadModuleArtifactFile(it->path());
    if (!artifact.has_value()) {
      continue;
    }

    state.modules.push_back(MakeModuleRecord(layout, *artifact));
  }

  if (ec) {
    throw std::runtime_error("failed to scan module artifacts under `"
      + ToUtf8PathString(layout.modules_dir) + "`: " + ec.message());
  }

  std::sort(
    state.modules.begin(), state.modules.end(), CompareBuildStateModules);
  return state;
}

auto LoadOrRecoverBuildStateFile(const std::filesystem::path& workspace_root,
  const BuildRootLayout& layout) -> BuildStateSnapshot
{
  try {
    const auto state = ReadBuildStateFile(layout.build_state_file);
    if (SnapshotMatchesLayout(state, workspace_root, layout)) {
      return state;
    }
  } catch (const std::exception&) {
  }

  return ScanModulesForBuildState(workspace_root, layout);
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
