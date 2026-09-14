//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>

namespace oxygen::content::import {

auto InspectSceneSource(const std::filesystem::path& source_path,
  const std::stop_token& stop_token) -> SceneSourceInspection
{
  SceneSourceInspection result;
  try {
    ImportRequest request {};
    request.source_path = source_path;
    request.options.coordinate.bake_transforms_into_meshes = false;
    const auto source_id = source_path.string();
    adapters::AdapterInput input {};
    input.source_id_prefix = source_id;
    input.request = std::move(request);
    input.stop_token = stop_token;
    switch (input.request.GetFormat()) {
    case ImportFormat::kGltf:
      return adapters::GltfAdapter::InspectSource(source_path, input);
    case ImportFormat::kFbx:
      return adapters::FbxAdapter::InspectSource(source_path, input);
    default:
      result.diagnostics.push_back({ .severity = ImportSeverity::kError,
        .code = "import.source.unsupported_format",
        .message = "Select a glTF, GLB or FBX source.",
        .source_path = source_id,
        .object_path = {} });
      break;
    }
  } catch (const std::exception& error) {
    result.diagnostics.push_back({ .severity = ImportSeverity::kError,
      .code = "import.source.inspection_failed",
      .message = error.what(),
      .source_path = source_path.string(),
      .object_path = {} });
  }
  return result;
}

auto ExportSceneSourceInspection(const SceneSourceInspection& report)
  -> std::string
{
  using nlohmann::json;
  auto diagnostics = json::array();
  for (const auto& diagnostic : report.diagnostics) {
    diagnostics.push_back({ { "severity", to_string(diagnostic.severity) },
      { "code", diagnostic.code }, { "message", diagnostic.message },
      { "source_path", diagnostic.source_path },
      { "object_path", diagnostic.object_path } });
  }
  return json {
    { "version", 1 }, { "parsed", report.parsed },
    { "supported", report.supported }, { "format", report.format },
    { "external_files", report.external_files },
    { "source_unit_meters",
      report.source_unit_meters ? json(*report.source_unit_meters)
                                : json(nullptr) },
    { "source_axes",
      { { "right", report.source_right }, { "up", report.source_up },
        { "front", report.source_front } } },
    { "source_left_handed",
      report.source_left_handed ? json(*report.source_left_handed)
                                : json(nullptr) },
    { "target_axes", { { "right", "+X" }, { "up", "+Z" }, { "front", "-Y" } } },
    { "target_unit_meters", 1.0 },
    { "reverses_winding", report.reverses_winding },
    { "mesh_count", report.mesh_count },
    { "material_count", report.material_count },
    { "node_count", report.node_count }, { "diagnostics", diagnostics }
  }.dump(2);
}

} // namespace oxygen::content::import
