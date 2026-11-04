//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>

namespace oxygen::examples::ui {

namespace {
  constexpr auto kGroupOrder = std::array {
    ImportSettingsGroup::kContent,
    ImportSettingsGroup::kGeometry,
    ImportSettingsGroup::kLightsAndCameras,
    ImportSettingsGroup::kTextures,
    ImportSettingsGroup::kImporter,
  };

  //! The group of a key of a record's settings; texture keys are every key
  //! of no other group.
  auto GroupOfSetting(const std::string_view key) -> ImportSettingsGroup
  {
    static constexpr auto kContentKeys = std::array<std::string_view, 4> {
      "content_flags",
      "node_pruning",
      "material_remap",
      "gltf_scene",
    };
    static constexpr auto kGeometryKeys = std::array<std::string_view, 11> {
      "bake_transforms",
      "unit_policy",
      "unit_scale",
      "up_axis",
      "normals_policy",
      "tangents_policy",
      "normals_smoothing_angle_deg",
      "generate_lods",
      "lod_levels",
      "lod_reduction_percent",
      "geometry_defaults",
    };
    static constexpr auto kLightKeys = std::array<std::string_view, 4> {
      "import_lights",
      "import_cameras",
      "light_intensity_scale",
      "omitted_light_range_m",
    };
    if (std::ranges::contains(kContentKeys, key)) {
      return ImportSettingsGroup::kContent;
    }
    if (std::ranges::contains(kGeometryKeys, key)) {
      return ImportSettingsGroup::kGeometry;
    }
    if (std::ranges::contains(kLightKeys, key)) {
      return ImportSettingsGroup::kLightsAndCameras;
    }
    return ImportSettingsGroup::kTextures;
  }

  auto InPanelOrder(const std::set<ImportSettingsGroup>& groups)
    -> std::vector<ImportSettingsGroup>
  {
    auto ordered = std::vector<ImportSettingsGroup> {};
    for (const auto group : kGroupOrder) {
      if (groups.contains(group)) {
        ordered.push_back(group);
      }
    }
    return ordered;
  }

  auto ParseObject(const std::string_view text) -> nlohmann::json
  {
    auto parsed = nlohmann::json::parse(text, nullptr, false);
    return parsed.is_object() ? parsed : nlohmann::json {};
  }

} // namespace

auto to_string(const ImportSettingsGroup group) noexcept -> std::string_view
{
  switch (group) {
  case ImportSettingsGroup::kContent:
    return "Content";
  case ImportSettingsGroup::kGeometry:
    return "Geometry";
  case ImportSettingsGroup::kLightsAndCameras:
    return "Lights and cameras";
  case ImportSettingsGroup::kTextures:
    return "Textures";
  case ImportSettingsGroup::kImporter:
    return "Importer";
  }
  return "Unknown";
}

auto SameImporterConfig(const content::import::AsyncImportService::Config& lhs,
  const content::import::AsyncImportService::Config& rhs) -> bool
{
  if (lhs.thread_pool_size != rhs.thread_pool_size) {
    return false;
  }
  return std::ranges::all_of(
    kImporterPipelines, [&lhs, &rhs](const ImporterPipeline& pipeline) -> bool {
      const auto& left = lhs.concurrency.*pipeline.member;
      const auto& right = rhs.concurrency.*pipeline.member;
      return left.workers == right.workers
        && left.queue_capacity == right.queue_capacity;
    });
}

auto MakeModelImportSettings(const ImportProfile& profile)
  -> content::import::SceneImportSettings
{
  auto options = profile.options;
  options.texture_tuning = profile.tuning;
  return content::import::SceneImportSettings::FromOptions(options);
}

auto ChangedGroups(const ImportProfile& profile)
  -> std::vector<ImportSettingsGroup>
{
  using content::import::ModelImportRecord;
  const auto defaults = DefaultImportProfile();
  const auto changed_settings = DifferingGroups(
    ModelImportRecord::MakeSettings(MakeModelImportSettings(defaults)),
    ModelImportRecord::MakeSettings(MakeModelImportSettings(profile)));
  auto groups = std::set<ImportSettingsGroup>(
    changed_settings.begin(), changed_settings.end());
  if (!SameImporterConfig(profile.importer, defaults.importer)) {
    groups.insert(ImportSettingsGroup::kImporter);
  }
  return InPanelOrder(groups);
}

auto ResetGroup(ImportProfile profile, const ImportSettingsGroup group)
  -> ImportProfile
{
  const auto defaults = DefaultImportProfile();
  auto& options = profile.options;
  const auto& initial = defaults.options;
  switch (group) {
  case ImportSettingsGroup::kContent:
    options.import_content = initial.import_content;
    options.node_pruning = initial.node_pruning;
    break;
  case ImportSettingsGroup::kGeometry:
    options.coordinate = initial.coordinate;
    options.normal_policy = initial.normal_policy;
    options.tangent_policy = initial.tangent_policy;
    options.normals_smoothing_angle_deg = initial.normals_smoothing_angle_deg;
    options.lod_generation = initial.lod_generation;
    break;
  case ImportSettingsGroup::kLightsAndCameras:
    options.import_lights = initial.import_lights;
    options.import_cameras = initial.import_cameras;
    options.light_intensity_scale = initial.light_intensity_scale;
    break;
  case ImportSettingsGroup::kTextures:
    profile.tuning = defaults.tuning;
    break;
  case ImportSettingsGroup::kImporter:
    profile.importer = defaults.importer;
    break;
  }
  return profile;
}

auto DifferingGroups(const std::string_view lhs, const std::string_view rhs)
  -> std::vector<ImportSettingsGroup>
{
  const auto left = ParseObject(lhs);
  const auto right = ParseObject(rhs);
  auto groups = std::set<ImportSettingsGroup> {};
  // A key differs when the other object lacks it or holds another value.
  for (const auto& [from, to] :
    { std::pair { &left, &right }, std::pair { &right, &left } }) {
    for (const auto& [key, value] : from->items()) {
      const auto other = to->find(key);
      if (other == to->end() || *other != value) {
        groups.insert(GroupOfSetting(key));
      }
    }
  }
  return InPanelOrder(groups);
}

} // namespace oxygen::examples::ui
