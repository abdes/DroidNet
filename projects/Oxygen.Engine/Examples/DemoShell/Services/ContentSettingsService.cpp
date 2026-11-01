//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples {

namespace {
  using content::import::ImportOptions;
  using TextureTuning = ImportOptions::TextureTuning;

  constexpr auto kMountSourcesKey = "content.mounts.sources";
  constexpr auto kImportRecordsKey = "content.mounts.import_records";

  constexpr auto kSceneKeyKey = "content.scene.key";
  constexpr auto kSceneVirtualPathKey = "content.scene.virtual_path";
  constexpr auto kSceneSourceKindKey = "content.scene.source_kind";
  constexpr auto kSceneSourcePathKey = "content.scene.source_path";
  constexpr auto kRecentScenesKey = "content.scene.recent";

  constexpr auto kImportTabMountKey = "content.import_tab.source_mount";
  constexpr auto kImportTabFilterKey = "content.import_tab.filter";
  constexpr auto kImportTabFbxKey = "content.import_tab.fbx";
  constexpr auto kImportTabGlbKey = "content.import_tab.glb";
  constexpr auto kImportTabGltfKey = "content.import_tab.gltf";
  constexpr auto kImportTabLoadSceneKey
    = "content.import_tab.load_scene_after_import";

  constexpr auto kPakTag = std::string_view { "pak" };
  constexpr auto kDirTag = std::string_view { "dir" };

  auto Settings() -> observer_ptr<SettingsService>
  {
    const auto settings = SettingsService::ForDemoApp();
    DCHECK_NOTNULL_F(settings);
    return settings;
  }

  auto KindTag(const data::CookedSourceKind kind) -> std::string_view
  {
    return kind == data::CookedSourceKind::kPak ? kPakTag : kDirTag;
  }

  auto KindOfTag(const std::string_view tag)
    -> std::optional<data::CookedSourceKind>
  {
    if (tag == kPakTag) {
      return data::CookedSourceKind::kPak;
    }
    if (tag == kDirTag) {
      return data::CookedSourceKind::kLooseCooked;
    }
    return std::nullopt;
  }

  //! The lines of `text`, without empty ones.
  auto Lines(const std::string_view text) -> std::vector<std::string_view>
  {
    auto lines = std::vector<std::string_view> {};
    std::size_t begin = 0;
    while (begin < text.size()) {
      const auto end = std::min(text.find('\n', begin), text.size());
      if (end > begin) {
        lines.push_back(text.substr(begin, end - begin));
      }
      begin = end + 1;
    }
    return lines;
  }

  auto JoinLines(const std::vector<std::string>& lines) -> std::string
  {
    auto text = std::string {};
    for (const auto& line : lines) {
      if (!text.empty()) {
        text.push_back('\n');
      }
      text += line;
    }
    return text;
  }

  //! `<tag> <encoded path>`.
  auto EncodeSource(const SettingsService& settings,
    const ContentSourceSetting& source) -> std::string
  {
    return std::string(KindTag(source.kind)) + " "
      + settings.EncodePath(source.path).generic_string();
  }

  auto DecodeSource(const SettingsService& settings,
    const std::string_view line) -> std::optional<ContentSourceSetting>
  {
    const auto space = line.find(' ');
    if (space == std::string_view::npos || space + 1 == line.size()) {
      return std::nullopt;
    }
    const auto kind = KindOfTag(line.substr(0, space));
    if (!kind) {
      return std::nullopt;
    }
    return ContentSourceSetting {
      .kind = *kind,
      .path = settings.DecodePath(std::string(line.substr(space + 1))),
    };
  }

  // --- Typed reads that keep the default for a missing or invalid value ---

  auto Read(const SettingsService& settings, const std::string_view key,
    bool& value) -> void
  {
    if (const auto stored = settings.GetBool(key)) {
      value = *stored;
    }
  }

  auto Read(const SettingsService& settings, const std::string_view key,
    float& value, const float first, const float last) -> void
  {
    if (const auto stored = settings.GetFloat(key)) {
      value = std::clamp(*stored, first, last);
    }
  }

  template <typename IntT>
  auto Read(const SettingsService& settings, const std::string_view key,
    IntT& value, const std::int64_t first, const std::int64_t last) -> void
  {
    if (const auto stored = settings.GetInt(key)) {
      value = static_cast<IntT>(std::clamp(*stored, first, last));
    }
  }

  //! Reads an enumerator stored by its value, one of `first`..`last`.
  template <typename EnumT>
  auto ReadEnum(const SettingsService& settings, const std::string_view key,
    EnumT& value, const EnumT first, const EnumT last) -> void
  {
    const auto stored = settings.GetInt(key);
    if (stored && *stored >= static_cast<std::int64_t>(first)
      && *stored <= static_cast<std::int64_t>(last)) {
      value = static_cast<EnumT>(*stored);
    }
  }

  template <typename EnumT>
  auto WriteEnum(SettingsService& settings, const std::string_view key,
    const EnumT value) -> void
  {
    settings.SetInt(key, static_cast<std::int64_t>(value));
  }

  auto ImportKey(const std::string_view leaf) -> std::string
  {
    return "content.import." + std::string(leaf);
  }

  auto TextureKey(const std::string_view leaf) -> std::string
  {
    return "content.import.texture." + std::string(leaf);
  }

  auto ImporterKey(const std::string_view leaf) -> std::string
  {
    return "content.importer." + std::string(leaf);
  }

  auto ReadOptions(const SettingsService& s, ImportOptions& o) -> void
  {
    using namespace content::import; // NOLINT(*-using-namespace)
    auto flags = static_cast<std::int64_t>(o.import_content);
    Read(s, ImportKey("content_flags"), flags, 0,
      static_cast<std::int64_t>(ImportContentFlags::kAll));
    o.import_content = static_cast<ImportContentFlags>(flags);
    Read(s, ImportKey("ignore_non_mesh"), o.ignore_non_mesh_primitives);
    ReadEnum(s, ImportKey("node_pruning"), o.node_pruning,
      NodePruningPolicy::kKeepAll, NodePruningPolicy::kDropEmptyNodes);
    ReadEnum(s, ImportKey("units"), o.coordinate.unit_normalization,
      UnitNormalizationPolicy::kNormalizeToMeters,
      UnitNormalizationPolicy::kApplyCustomFactor);
    Read(s, ImportKey("unit_scale"), o.coordinate.unit_scale, 0.001F, 1000.0F);
    ReadEnum(s, ImportKey("up_axis"), o.coordinate.up_axis,
      SourceUpAxis::kFromFile, SourceUpAxis::kZ);
    ReadEnum(s, ImportKey("normal_policy"), o.normal_policy,
      GeometryAttributePolicy::kNone,
      GeometryAttributePolicy::kAlwaysRecalculate);
    ReadEnum(s, ImportKey("tangent_policy"), o.tangent_policy,
      GeometryAttributePolicy::kNone,
      GeometryAttributePolicy::kAlwaysRecalculate);
    Read(s, ImportKey("smoothing_angle"), o.normals_smoothing_angle_deg, 0.0F,
      180.0F);
    Read(s, ImportKey("generate_lods"), o.lod_generation.enabled);
    Read(s, ImportKey("lod_levels"), o.lod_generation.levels,
      LodGenerationOptions::kMinLevels, LodGenerationOptions::kMaxLevels);
    Read(s, ImportKey("lod_reduction"), o.lod_generation.reduction_percent,
      LodGenerationOptions::kMinReductionPercent,
      LodGenerationOptions::kMaxReductionPercent);
    Read(s, ImportKey("lights"), o.import_lights);
    Read(s, ImportKey("cameras"), o.import_cameras);
    Read(s, ImportKey("light_intensity_scale"), o.light_intensity_scale, 0.0F,
      1000.0F);
  }

  auto WriteOptions(SettingsService& s, const ImportOptions& o) -> void
  {
    s.SetInt(
      ImportKey("content_flags"), static_cast<std::int64_t>(o.import_content));
    s.SetBool(ImportKey("ignore_non_mesh"), o.ignore_non_mesh_primitives);
    WriteEnum(s, ImportKey("node_pruning"), o.node_pruning);
    WriteEnum(s, ImportKey("units"), o.coordinate.unit_normalization);
    s.SetFloat(ImportKey("unit_scale"), o.coordinate.unit_scale);
    WriteEnum(s, ImportKey("up_axis"), o.coordinate.up_axis);
    WriteEnum(s, ImportKey("normal_policy"), o.normal_policy);
    WriteEnum(s, ImportKey("tangent_policy"), o.tangent_policy);
    s.SetFloat(ImportKey("smoothing_angle"), o.normals_smoothing_angle_deg);
    s.SetBool(ImportKey("generate_lods"), o.lod_generation.enabled);
    s.SetInt(ImportKey("lod_levels"), o.lod_generation.levels);
    s.SetFloat(ImportKey("lod_reduction"), o.lod_generation.reduction_percent);
    s.SetBool(ImportKey("lights"), o.import_lights);
    s.SetBool(ImportKey("cameras"), o.import_cameras);
    s.SetFloat(ImportKey("light_intensity_scale"), o.light_intensity_scale);
  }

  auto ReadTuning(const SettingsService& s, TextureTuning& t) -> void
  {
    using namespace content::import; // NOLINT(*-using-namespace)
    Read(s, TextureKey("enabled"), t.enabled);
    ReadEnum(s, TextureKey("intent"), t.intent, TextureIntent::kAlbedo,
      TextureIntent::kHeightMap);
    ReadEnum(s, TextureKey("color_space"), t.source_color_space,
      ColorSpace::kLinear, ColorSpace::kSRGB);
    ReadEnum(s, TextureKey("mip_policy"), t.mip_policy, MipPolicy::kNone,
      MipPolicy::kMaxCount);
    Read(s, TextureKey("max_mips"), t.max_mip_levels, 1, 16);
    Read(s, TextureKey("max_size"), t.max_size, 0, 16384);
    ReadEnum(s, TextureKey("mip_filter"), t.mip_filter, MipFilter::kBox,
      MipFilter::kLanczos);
    ReadEnum(s, TextureKey("color_format"), t.color_output_format,
      Format::kUnknown, Format::kMaxFormat);
    ReadEnum(s, TextureKey("data_format"), t.data_output_format,
      Format::kUnknown, Format::kMaxFormat);
    ReadEnum(s, TextureKey("bc7_quality"), t.bc7_quality, Bc7Quality::kNone,
      Bc7Quality::kHigh);
    ReadEnum(s, TextureKey("hdr_handling"), t.hdr_handling, HdrHandling::kError,
      HdrHandling::kKeepFloat);
    Read(s, TextureKey("bake_hdr"), t.bake_hdr_to_ldr);
    Read(s, TextureKey("exposure_ev"), t.exposure_ev, -10.0F, 10.0F);
    Read(s, TextureKey("flip_normal_green"), t.flip_normal_green);
    Read(s, TextureKey("renormalize_mips"), t.renormalize_normals_in_mips);
    Read(s, TextureKey("cubemap"), t.import_cubemap);
    Read(s, TextureKey("equirect"), t.equirect_to_cubemap);
    Read(s, TextureKey("face_size"), t.cubemap_face_size, 0, 8192);
    ReadEnum(s, TextureKey("cube_layout"), t.cubemap_layout,
      CubeMapImageLayout::kUnknown, CubeMapImageLayout::kVerticalCross);
  }

  auto WriteTuning(SettingsService& s, const TextureTuning& t) -> void
  {
    s.SetBool(TextureKey("enabled"), t.enabled);
    WriteEnum(s, TextureKey("intent"), t.intent);
    WriteEnum(s, TextureKey("color_space"), t.source_color_space);
    WriteEnum(s, TextureKey("mip_policy"), t.mip_policy);
    s.SetInt(TextureKey("max_mips"), t.max_mip_levels);
    s.SetInt(TextureKey("max_size"), t.max_size);
    WriteEnum(s, TextureKey("mip_filter"), t.mip_filter);
    WriteEnum(s, TextureKey("color_format"), t.color_output_format);
    WriteEnum(s, TextureKey("data_format"), t.data_output_format);
    WriteEnum(s, TextureKey("bc7_quality"), t.bc7_quality);
    WriteEnum(s, TextureKey("hdr_handling"), t.hdr_handling);
    s.SetBool(TextureKey("bake_hdr"), t.bake_hdr_to_ldr);
    s.SetFloat(TextureKey("exposure_ev"), t.exposure_ev);
    s.SetBool(TextureKey("flip_normal_green"), t.flip_normal_green);
    s.SetBool(TextureKey("renormalize_mips"), t.renormalize_normals_in_mips);
    s.SetBool(TextureKey("cubemap"), t.import_cubemap);
    s.SetBool(TextureKey("equirect"), t.equirect_to_cubemap);
    s.SetInt(TextureKey("face_size"), t.cubemap_face_size);
    WriteEnum(s, TextureKey("cube_layout"), t.cubemap_layout);
  }

  auto ReadImporter(const SettingsService& s,
    content::import::AsyncImportService::Config& c) -> void
  {
    Read(s, ImporterKey("thread_pool"), c.thread_pool_size, 1, 128);
    for (const auto& pipeline : kImporterPipelines) {
      auto& concurrency = c.concurrency.*pipeline.member;
      const auto prefix = std::string(pipeline.name);
      Read(s, ImporterKey(prefix + ".workers"), concurrency.workers, 1, 64);
      Read(
        s, ImporterKey(prefix + ".queue"), concurrency.queue_capacity, 1, 256);
    }
  }

  auto WriteImporter(SettingsService& s,
    const content::import::AsyncImportService::Config& c) -> void
  {
    s.SetInt(ImporterKey("thread_pool"), c.thread_pool_size);
    for (const auto& pipeline : kImporterPipelines) {
      const auto& concurrency = c.concurrency.*pipeline.member;
      const auto prefix = std::string(pipeline.name);
      s.SetInt(ImporterKey(prefix + ".workers"), concurrency.workers);
      s.SetInt(ImporterKey(prefix + ".queue"), concurrency.queue_capacity);
    }
  }
} // namespace

auto DefaultImportProfile() -> ImportProfile
{
  auto profile = ImportProfile {};
  // Interactive imports keep full resolution and complete mip chains while
  // using the faster filter and encoder tiers.
  profile.tuning.enabled = true;
  profile.tuning.mip_policy = content::import::MipPolicy::kFullChain;
  profile.tuning.mip_filter = content::import::MipFilter::kBox;
  profile.tuning.bc7_quality = content::import::Bc7Quality::kFast;

  auto& importer = profile.importer;
  importer.thread_pool_size = 35;
  importer.max_in_flight_jobs = 35;
  importer.concurrency.texture = { .workers = 12, .queue_capacity = 64 };
  importer.concurrency.buffer = { .workers = 2, .queue_capacity = 64 };
  importer.concurrency.material = { .workers = 4, .queue_capacity = 64 };
  importer.concurrency.mesh_build = { .workers = 12, .queue_capacity = 128 };
  importer.concurrency.geometry = { .workers = 8, .queue_capacity = 64 };
  importer.concurrency.scene = { .workers = 1, .queue_capacity = 8 };
  return profile;
}

auto ContentSettingsService::GetMountSources() const
  -> std::vector<ContentSourceSetting>
{
  const auto settings = Settings();
  const auto text = settings->GetString(kMountSourcesKey).value_or("");
  auto sources = std::vector<ContentSourceSetting> {};
  for (const auto line : Lines(text)) {
    if (auto source = DecodeSource(*settings, line)) {
      sources.push_back(std::move(*source));
    } else {
      LOG_F(WARNING, "Content settings: ignoring the mount entry '{}'", line);
    }
  }
  return sources;
}

auto ContentSettingsService::SetMountSources(
  const std::vector<ContentSourceSetting>& sources) -> void
{
  const auto settings = Settings();
  auto lines = std::vector<std::string> {};
  for (const auto& source : sources) {
    if (!source.path.empty()) {
      lines.push_back(EncodeSource(*settings, source));
    }
  }
  settings->SetString(kMountSourcesKey, JoinLines(lines));
  BumpEpoch();
}

auto ContentSettingsService::GetMountedImportRecords() const
  -> std::vector<std::filesystem::path>
{
  const auto settings = Settings();
  const auto text = settings->GetString(kImportRecordsKey).value_or("");
  auto records = std::vector<std::filesystem::path> {};
  for (const auto line : Lines(text)) {
    records.push_back(settings->DecodePath(std::string(line)));
  }
  return records;
}

auto ContentSettingsService::SetMountedImportRecords(
  const std::vector<std::filesystem::path>& records) -> void
{
  const auto settings = Settings();
  auto lines = std::vector<std::string> {};
  for (const auto& record : records) {
    if (!record.empty()) {
      lines.push_back(settings->EncodePath(record).generic_string());
    }
  }
  settings->SetString(kImportRecordsKey, JoinLines(lines));
  BumpEpoch();
}

auto ContentSettingsService::GetLoadedScene() const
  -> std::optional<ContentSceneSetting>
{
  const auto settings = Settings();
  const auto key = settings->GetString(kSceneKeyKey).value_or("");
  const auto path = settings->GetString(kSceneSourcePathKey).value_or("");
  const auto kind = KindOfTag(
    settings->GetString(kSceneSourceKindKey).value_or(std::string {}));
  if (key.empty() || path.empty() || !kind) {
    return std::nullopt;
  }
  return ContentSceneSetting {
    .scene_key = key,
    .virtual_path = settings->GetString(kSceneVirtualPathKey).value_or(""),
    .source = { .kind = *kind, .path = settings->DecodePath(path) },
  };
}

auto ContentSettingsService::SetLoadedScene(
  const std::optional<ContentSceneSetting>& scene) -> void
{
  const auto settings = Settings();
  if (!scene) {
    settings->Remove(kSceneKeyKey);
    settings->Remove(kSceneVirtualPathKey);
    settings->Remove(kSceneSourceKindKey);
    settings->Remove(kSceneSourcePathKey);
  } else {
    settings->SetString(kSceneKeyKey, scene->scene_key);
    settings->SetString(kSceneVirtualPathKey, scene->virtual_path);
    settings->SetString(
      kSceneSourceKindKey, std::string(KindTag(scene->source.kind)));
    settings->SetString(kSceneSourcePathKey,
      settings->EncodePath(scene->source.path).generic_string());
  }
  BumpEpoch();
}

auto ContentSettingsService::GetRecentScenes() const
  -> std::vector<RecentSceneSetting>
{
  // One `<key> <virtual path>` line per scene; keys hold no spaces.
  const auto text = Settings()->GetString(kRecentScenesKey).value_or("");
  auto scenes = std::vector<RecentSceneSetting> {};
  for (const auto line : Lines(text)) {
    const auto space = line.find(' ');
    if (space == std::string_view::npos || space == 0
      || space + 1 == line.size()) {
      continue;
    }
    scenes.push_back({
      .scene_key = std::string(line.substr(0, space)),
      .virtual_path = std::string(line.substr(space + 1)),
    });
    if (scenes.size() == kRecentSceneCount) {
      break;
    }
  }
  return scenes;
}

auto ContentSettingsService::SetRecentScenes(
  const std::vector<RecentSceneSetting>& scenes) -> void
{
  auto lines = std::vector<std::string> {};
  for (const auto& scene : scenes) {
    if (lines.size() == kRecentSceneCount) {
      break;
    }
    if (!scene.scene_key.empty() && !scene.virtual_path.empty()) {
      lines.push_back(scene.scene_key + " " + scene.virtual_path);
    }
  }
  Settings()->SetString(kRecentScenesKey, JoinLines(lines));
  BumpEpoch();
}

auto ContentSettingsService::GetImportProfile() const -> ImportProfile
{
  const auto settings = Settings();
  auto profile = DefaultImportProfile();
  ReadOptions(*settings, profile.options);
  ReadTuning(*settings, profile.tuning);
  ReadImporter(*settings, profile.importer);
  return profile;
}

auto ContentSettingsService::SetImportProfile(const ImportProfile& profile)
  -> void
{
  const auto settings = Settings();
  WriteOptions(*settings, profile.options);
  WriteTuning(*settings, profile.tuning);
  WriteImporter(*settings, profile.importer);
  BumpEpoch();
}

auto ContentSettingsService::GetImportTab() const -> ContentImportTabSettings
{
  const auto settings = Settings();
  auto tab = ContentImportTabSettings {};
  tab.source_mount = settings->GetString(kImportTabMountKey).value_or("");
  tab.filter = settings->GetString(kImportTabFilterKey).value_or("");
  Read(*settings, kImportTabFbxKey, tab.fbx);
  Read(*settings, kImportTabGlbKey, tab.glb);
  Read(*settings, kImportTabGltfKey, tab.gltf);
  Read(*settings, kImportTabLoadSceneKey, tab.load_scene_after_import);
  return tab;
}

auto ContentSettingsService::SetImportTab(
  const ContentImportTabSettings& settings_value) -> void
{
  const auto settings = Settings();
  settings->SetString(kImportTabMountKey, settings_value.source_mount);
  settings->SetString(kImportTabFilterKey, settings_value.filter);
  settings->SetBool(kImportTabFbxKey, settings_value.fbx);
  settings->SetBool(kImportTabGlbKey, settings_value.glb);
  settings->SetBool(kImportTabGltfKey, settings_value.gltf);
  settings->SetBool(
    kImportTabLoadSceneKey, settings_value.load_scene_after_import);
  BumpEpoch();
}

auto ContentSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto ContentSettingsService::BumpEpoch() noexcept -> void
{
  epoch_.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace oxygen::examples
