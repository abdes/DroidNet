//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>

namespace oxygen::content::import {

namespace {

  using import::GeometryAttributePolicy;
  using import::ImportContentFlags;
  using import::ImportFormat;
  using import::NodePruningPolicy;
  using import::SourceUpAxis;
  using import::UnitNormalizationPolicy;

  auto ParseUnitPolicy(std::string_view value)
    -> std::optional<UnitNormalizationPolicy>
  {
    if (value == "normalize") {
      return UnitNormalizationPolicy::kNormalizeToMeters;
    }
    if (value == "preserve") {
      return UnitNormalizationPolicy::kPreserveSource;
    }
    if (value == "custom") {
      return UnitNormalizationPolicy::kApplyCustomFactor;
    }
    return std::nullopt;
  }

  auto ParseGeometryPolicy(std::string_view value)
    -> std::optional<GeometryAttributePolicy>
  {
    if (value == "none") {
      return GeometryAttributePolicy::kNone;
    }
    if (value == "preserve") {
      return GeometryAttributePolicy::kPreserveIfPresent;
    }
    if (value == "generate") {
      return GeometryAttributePolicy::kGenerateMissing;
    }
    if (value == "recalculate") {
      return GeometryAttributePolicy::kAlwaysRecalculate;
    }
    return std::nullopt;
  }

  auto ParseUpAxis(std::string_view value) -> std::optional<SourceUpAxis>
  {
    if (value == "from-file") {
      return SourceUpAxis::kFromFile;
    }
    if (value == "y") {
      return SourceUpAxis::kY;
    }
    if (value == "z") {
      return SourceUpAxis::kZ;
    }
    return std::nullopt;
  }

  auto ParseNodePruning(std::string_view value)
    -> std::optional<NodePruningPolicy>
  {
    if (value == "keep") {
      return NodePruningPolicy::kKeepAll;
    }
    if (value == "drop-empty") {
      return NodePruningPolicy::kDropEmptyNodes;
    }
    return std::nullopt;
  }

  auto BuildContentFlags(const SceneImportSettings& settings)
    -> ImportContentFlags
  {
    ImportContentFlags flags = ImportContentFlags::kNone;
    if (settings.import_textures) {
      flags |= ImportContentFlags::kTextures;
    }
    if (settings.import_materials) {
      flags |= ImportContentFlags::kMaterials;
    }
    if (settings.import_geometry) {
      flags |= ImportContentFlags::kGeometry;
    }
    if (settings.import_scene) {
      flags |= ImportContentFlags::kScene;
    }
    return flags;
  }

  auto FormatName(const ImportFormat format) -> std::string_view
  {
    switch (format) {
    case ImportFormat::kFbx:
      return "fbx";
    case ImportFormat::kGltf:
      return "gltf";
    case ImportFormat::kTextureImage:
      return "texture";
    case ImportFormat::kUnknown:
      return "unknown";
    }
    return "unknown";
  }

} // namespace

auto SceneImportSettings::Prepare(const ImportFormat expected_format,
  std::vector<ImportDiagnostic>& diagnostics) const
  -> std::optional<ImportRequest>
{
  using internal::ParseOption;
  using internal::RefuseSetting;

  const auto& settings = *this;
  ImportRequest request {};
  request.source_path = settings.source_path;
  if (settings.virtual_path.empty()) {
    RefuseSetting(diagnostics, request_codes::kVirtualPathMissing,
      "a model import requires 'virtual_path', the identity of its source file",
      "/virtual_path");
    return std::nullopt;
  }
  request.asset_virtual_path = settings.virtual_path;
  if (settings.model_record) {
    request.surface_provenance = settings.model_record->surface_provenance;
    request.model_import = ModelImportInputs {
      .node_continuity = settings.model_record->node_continuity,
      .published_nodes = settings.model_record->published_nodes,
      .vanished_nodes = settings.model_record->vanished_nodes,
    };
  } else {
    request.surface_provenance
      = std::make_shared<const SurfaceProvenance>(Uuid::Generate());
  }
  internal::ApplyJobName(settings.job_name, request);

  if (expected_format != ImportFormat::kUnknown
    && request.GetFormat() != expected_format) {
    RefuseSetting(diagnostics, request_codes::kSourceFormatMismatch,
      fmt::format("source file is not a {} asset", FormatName(expected_format)),
      "/source");
    return std::nullopt;
  }

  if (!std::isfinite(settings.gltf_omitted_light_range_m)
    || settings.gltf_omitted_light_range_m <= 0.0F
    || !std::isfinite(1.0F / settings.gltf_omitted_light_range_m)) {
    RefuseSetting(diagnostics, request_codes::kOptionOutOfRange,
      "omitted light range must be finite and positive",
      "/gltf_omitted_light_range_m");
    return std::nullopt;
  }
  auto options = request.options;
  options.gltf_omitted_light_range_m = settings.gltf_omitted_light_range_m;
  options.gltf_scene = settings.gltf_scene;
  options.import_lights = settings.import_lights;
  options.import_cameras = settings.import_cameras;
  options.light_intensity_scale = settings.light_intensity_scale;
  options.normals_smoothing_angle_deg = settings.normals_smoothing_angle_deg;
  options.lod_generation = {
    .enabled = settings.generate_lods,
    .levels = settings.lod_levels,
    .reduction_percent = settings.lod_reduction_percent,
  };
  options.material_remap.insert(
    settings.material_remap.begin(), settings.material_remap.end());
  options.geometry_defaults.insert(
    settings.geometry_defaults.begin(), settings.geometry_defaults.end());
  options.import_content = BuildContentFlags(settings);
  options.coordinate.bake_transforms_into_meshes = settings.bake_transforms;

  if (!settings.unit_policy.empty()) {
    const auto parsed = ParseOption(
      "unit_policy", settings.unit_policy, ParseUnitPolicy, diagnostics);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    options.coordinate.unit_normalization = *parsed;
  }

  if (settings.unit_scale_set) {
    if (options.coordinate.unit_normalization
      != UnitNormalizationPolicy::kApplyCustomFactor) {
      RefuseSetting(diagnostics, request_codes::kUnitScaleNeedsCustomPolicy,
        "unit_scale requires unit_policy 'custom'", "/unit_scale");
      return std::nullopt;
    }
    options.coordinate.unit_scale = settings.unit_scale;
  }

  if (!settings.up_axis.empty()) {
    const auto parsed
      = ParseOption("up_axis", settings.up_axis, ParseUpAxis, diagnostics);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    options.coordinate.up_axis = *parsed;
  }

  if (!std::isfinite(settings.light_intensity_scale)
    || settings.light_intensity_scale < 0.0F) {
    RefuseSetting(diagnostics, request_codes::kOptionOutOfRange,
      "light intensity scale must be finite and not negative",
      "/light_intensity_scale");
    return std::nullopt;
  }

  if (!std::isfinite(settings.normals_smoothing_angle_deg)
    || settings.normals_smoothing_angle_deg < 0.0F
    || settings.normals_smoothing_angle_deg > 180.0F) {
    RefuseSetting(diagnostics, request_codes::kOptionOutOfRange,
      "normals smoothing angle must be from 0 to 180 degrees",
      "/normals_smoothing_angle_deg");
    return std::nullopt;
  }

  if (settings.lod_levels < LodGenerationOptions::kMinLevels
    || settings.lod_levels > LodGenerationOptions::kMaxLevels) {
    RefuseSetting(diagnostics, request_codes::kOptionOutOfRange,
      fmt::format("LOD levels must be from {} to {}",
        LodGenerationOptions::kMinLevels, LodGenerationOptions::kMaxLevels),
      "/lod_levels");
    return std::nullopt;
  }

  if (!std::isfinite(settings.lod_reduction_percent)
    || settings.lod_reduction_percent
      < LodGenerationOptions::kMinReductionPercent
    || settings.lod_reduction_percent
      > LodGenerationOptions::kMaxReductionPercent) {
    RefuseSetting(diagnostics, request_codes::kOptionOutOfRange,
      fmt::format("LOD reduction must be from {} to {} percent",
        LodGenerationOptions::kMinReductionPercent,
        LodGenerationOptions::kMaxReductionPercent),
      "/lod_reduction_percent");
    return std::nullopt;
  }

  if (!settings.normals_policy.empty()) {
    const auto parsed = ParseOption("normals_policy", settings.normals_policy,
      ParseGeometryPolicy, diagnostics);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    options.normal_policy = *parsed;
  }

  if (!settings.tangents_policy.empty()) {
    const auto parsed = ParseOption("tangents_policy", settings.tangents_policy,
      ParseGeometryPolicy, diagnostics);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    options.tangent_policy = *parsed;
  }

  if (!settings.node_pruning.empty()) {
    const auto parsed = ParseOption(
      "node_pruning", settings.node_pruning, ParseNodePruning, diagnostics);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    options.node_pruning = *parsed;
  }

  // Handle texture tuning overrides for the scene
  if (!internal::MapSettingsToTuning(
        settings.texture_defaults, options.texture_tuning, diagnostics)) {
    return std::nullopt;
  }

  for (const auto& [name, tex_settings] : settings.texture_overrides) {
    ImportOptions::TextureTuning tuning = options.texture_tuning;
    const auto first_refusal = diagnostics.size();
    if (!internal::MapSettingsToTuning(tex_settings, tuning, diagnostics)) {
      const auto override_pointer
        = (nlohmann::json::json_pointer {} / "texture_overrides" / name)
            .to_string();
      for (auto index = first_refusal; index < diagnostics.size(); ++index) {
        diagnostics[index].json_pointer.insert(0, override_pointer);
      }
      return std::nullopt;
    }
    options.texture_overrides[name] = std::move(tuning);
  }

  request.options = std::move(options);
  return request;
}

namespace internal {
  auto BuildSceneRequest(const SceneImportSettings& settings,
    const ImportFormat expected_format,
    std::vector<ImportDiagnostic>& diagnostics,
    std::shared_ptr<const CapturedInputSet> captured_inputs)
    -> std::optional<ImportRequest>
  {
    auto request = settings.Prepare(expected_format, diagnostics);
    if (!request.has_value()
      || !RequireCookedRoot(settings.cooked_root, *request, diagnostics)) {
      return std::nullopt;
    }
    request->captured_inputs = std::move(captured_inputs);
    return request;
  }
} // namespace internal

} // namespace oxygen::content::import
