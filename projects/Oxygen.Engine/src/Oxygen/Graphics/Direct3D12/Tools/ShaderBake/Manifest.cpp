//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  using nlohmann::json;

  constexpr uint32_t kManifestSchemaVersion = 1;

  auto FormatRequestKeyHex(const uint64_t request_key) -> std::string
  {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << request_key;
    return stream.str();
  }

  auto ParseRequestKeyHex(const std::string& text) -> uint64_t
  {
    if (text.size() != 16) {
      throw std::runtime_error(
        "request_key_hex must be 16 lowercase hex characters");
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
          "request_key_hex contains a non-hex character");
      }
    }

    return value;
  }

  auto ShaderStageToManifestString(const ShaderType stage) -> std::string_view
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

    throw std::runtime_error("unsupported shader stage in manifest");
  }

  auto ManifestStringToShaderStage(const std::string& stage) -> ShaderType
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

    throw std::runtime_error("unsupported shader stage string in manifest");
  }

  auto SerializeDefines(std::span<const ShaderDefine> defines) -> json
  {
    auto result = json::array();
    for (const auto& define : defines) {
      result.push_back(json {
        { "name", define.name },
        { "value", define.value.value_or("1") },
      });
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
      defines.push_back(ShaderDefine {
        .name = define_json.at("name").get<std::string>(),
        .value = define_json.at("value").get<std::string>(),
      });
    }
    return defines;
  }

} // namespace

auto BuildManifestSnapshot(std::span<const ExpandedShaderRequest> requests)
  -> ManifestSnapshot
{
  ManifestSnapshot snapshot;
  snapshot.requests.reserve(requests.size());

  for (const auto& expanded_request : requests) {
    snapshot.requests.push_back(ManifestRequestRecord {
      .request = expanded_request.request,
      .request_key = expanded_request.request_key,
    });
  }

  return snapshot;
}

auto WriteManifestFile(const std::filesystem::path& manifest_path,
  const ManifestSnapshot& snapshot) -> void
{
  auto requests_json = json::array();
  for (const auto& request : snapshot.requests) {
    requests_json.push_back(json {
      { "request_key_hex", FormatRequestKeyHex(request.request_key) },
      { "source_path", request.request.source_path },
      { "entry_point", request.request.entry_point },
      { "stage",
        std::string(ShaderStageToManifestString(request.request.stage)) },
      { "defines", SerializeDefines(request.request.defines) },
    });
  }

  auto payload = json {
    { "schema_version", kManifestSchemaVersion },
    { "request_count", snapshot.requests.size() },
    { "requests", std::move(requests_json) },
  };

  auto serialized = payload.dump(2);
  serialized.push_back('\n');
  WriteTextFileAtomically(manifest_path, serialized);
}

auto ReadManifestFile(const std::filesystem::path& manifest_path)
  -> ManifestSnapshot
{
  std::ifstream stream(manifest_path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error(
      "failed to open manifest `" + ToUtf8PathString(manifest_path) + "`");
  }

  try {
    const auto payload = json::parse(stream);
    const auto schema_version = payload.at("schema_version").get<uint32_t>();
    if (schema_version != kManifestSchemaVersion) {
      throw std::runtime_error("unsupported manifest schema version");
    }

    const auto& requests_json = payload.at("requests");
    if (!requests_json.is_array()) {
      throw std::runtime_error("manifest requests must be an array");
    }

    const auto request_count = payload.at("request_count").get<size_t>();
    if (request_count != requests_json.size()) {
      throw std::runtime_error(
        "manifest request_count does not match requests size");
    }

    ManifestSnapshot snapshot;
    snapshot.requests.reserve(requests_json.size());

    for (const auto& request_json : requests_json) {
      snapshot.requests.push_back(ManifestRequestRecord {
        .request = ShaderRequest {
          .stage
          = ManifestStringToShaderStage(request_json.at("stage").get<std::string>()),
          .source_path = request_json.at("source_path").get<std::string>(),
          .entry_point = request_json.at("entry_point").get<std::string>(),
          .defines = ParseDefines(request_json.at("defines")),
        },
        .request_key = ParseRequestKeyHex(
          request_json.at("request_key_hex").get<std::string>()),
      });
    }

    return snapshot;
  } catch (const std::exception& e) {
    throw std::runtime_error("failed to read manifest `"
      + ToUtf8PathString(manifest_path) + "`: " + e.what());
  }
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
