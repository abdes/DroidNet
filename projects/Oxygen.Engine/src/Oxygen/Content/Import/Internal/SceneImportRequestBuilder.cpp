//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Content/Import/Internal/Utils/ImportSettingsUtils.h>

namespace oxygen::content::import::internal {

namespace {

  using import::GeometryAttributePolicy;
  using import::ImportContentFlags;
  using import::ImportFormat;
  using import::NodePruningPolicy;
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

auto BuildSceneRequest(const SceneImportSettings& settings,
  const ImportFormat expected_format, std::ostream& error_stream)
  -> std::optional<ImportRequest>
{
  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  ImportRequest request {};
  request.source_path = settings.source_path;

  std::filesystem::path root(settings.cooked_root);
  if (!root.is_absolute()) {
    error_stream << "ERROR: cooked root must be an absolute path\n";
    return std::nullopt;
  }
  request.cooked_root = root;

  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  } else {
    const auto stem = request.source_path.stem().string();
    if (!stem.empty()) {
      request.job_name = stem;
    }
  }

  if (expected_format != ImportFormat::kUnknown) {
    const auto actual_format = request.GetFormat();
    if (actual_format != expected_format) {
      error_stream << "ERROR: source file is not a "
                   << FormatName(expected_format) << " asset\n";
      return std::nullopt;
    }
  }

  auto options = request.options;
  options.import_content = BuildContentFlags(settings);
  options.coordinate.bake_transforms_into_meshes = settings.bake_transforms;
  options.with_content_hashing = settings.with_content_hashing;

  if (!settings.unit_policy.empty()) {
    const auto parsed = ParseUnitPolicy(settings.unit_policy);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --unit-policy value\n";
      return std::nullopt;
    }
    options.coordinate.unit_normalization = *parsed;
  }

  if (settings.unit_scale_set) {
    if (options.coordinate.unit_normalization
      != UnitNormalizationPolicy::kApplyCustomFactor) {
      error_stream << "ERROR: --unit-scale requires --unit-policy=custom\n";
      return std::nullopt;
    }
    options.coordinate.unit_scale = settings.unit_scale;
  }

  if (!settings.normals_policy.empty()) {
    const auto parsed = ParseGeometryPolicy(settings.normals_policy);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --normals value\n";
      return std::nullopt;
    }
    options.normal_policy = *parsed;
  }

  if (!settings.tangents_policy.empty()) {
    const auto parsed = ParseGeometryPolicy(settings.tangents_policy);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --tangents value\n";
      return std::nullopt;
    }
    options.tangent_policy = *parsed;
  }

  if (!settings.node_pruning.empty()) {
    const auto parsed = ParseNodePruning(settings.node_pruning);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --prune-nodes value\n";
      return std::nullopt;
    }
    options.node_pruning = *parsed;
  }

  // Handle texture tuning overrides for the scene
  if (!MapSettingsToTuning(
        settings.texture_defaults, options.texture_tuning, error_stream)) {
    return std::nullopt;
  }

  for (const auto& [name, tex_settings] : settings.texture_overrides) {
    ImportOptions::TextureTuning tuning = options.texture_tuning;
    if (!MapSettingsToTuning(tex_settings, tuning, error_stream)) {
      return std::nullopt;
    }
    options.texture_overrides[name] = std::move(tuning);
  }

  request.options = std::move(options);
  return request;
}

} // namespace oxygen::content::import::internal
