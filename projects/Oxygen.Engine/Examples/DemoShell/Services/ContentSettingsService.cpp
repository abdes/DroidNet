//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

namespace {
  // Explorer Keys
  constexpr auto kModelRootKey = "content.explorer.model_root";
  constexpr auto kIncludeFbxKey = "content.explorer.include_fbx";
  constexpr auto kIncludeGlbKey = "content.explorer.include_glb";
  constexpr auto kIncludeGltfKey = "content.explorer.include_gltf";
  constexpr auto kAutoLoadOnImportKey = "content.explorer.auto_load_on_import";
  constexpr auto kAutoDumpTexMemKey = "content.explorer.auto_dump_tex_mem";
  constexpr auto kAutoDumpDelayKey = "content.explorer.auto_dump_delay";
  constexpr auto kDumpTopNKey = "content.explorer.dump_top_n";

  // Import Options Keys (subset of most important)
  constexpr auto kAssetKeyPolicyKey = "content.import.asset_key_policy";
  constexpr auto kNodePruningKey = "content.import.node_pruning";
  constexpr auto kImportContentFlagsKey = "content.import.content_flags";
  constexpr auto kWithHashingKey = "content.import.with_hashing";
  constexpr auto kNormalPolicyKey = "content.import.normal_policy";
  constexpr auto kTangentPolicyKey = "content.import.tangent_policy";

  // Paths
  constexpr auto kLastCookedOutputKey = "content.paths.last_cooked_output";

  // Texture Tuning Keys
  constexpr auto kTexTuningEnabledKey = "content.import.tuning.enabled";
  constexpr auto kTexTuningIntentKey = "content.import.tuning.intent";
  constexpr auto kTexTuningColorSpaceKey = "content.import.tuning.color_space";
  constexpr auto kTexTuningMipPolicyKey = "content.import.tuning.mip_policy";
  constexpr auto kTexTuningMaxMipsKey = "content.import.tuning.max_mips";
  constexpr auto kTexTuningMipFilterKey = "content.import.tuning.mip_filter";
  constexpr auto kTexTuningColorFormatKey
    = "content.import.tuning.color_format";
  constexpr auto kTexTuningDataFormatKey = "content.import.tuning.data_format";
  constexpr auto kTexTuningBc7QualityKey = "content.import.tuning.bc7_quality";
  constexpr auto kTexTuningHdrHandlingKey
    = "content.import.tuning.hdr_handling";

  // Layout Keys
  constexpr auto kLayoutVirtualRootKey = "content.layout.virtual_root";
  constexpr auto kLayoutIndexNameKey = "content.layout.index_name";
  constexpr auto kLayoutResourcesDirKey = "content.layout.resources_dir";
  constexpr auto kLayoutDescriptorsDirKey = "content.layout.descriptors_dir";
  constexpr auto kLayoutScenesSubdirKey = "content.layout.scenes_subdir";
  constexpr auto kLayoutGeometrySubdirKey = "content.layout.geometry_subdir";
  constexpr auto kLayoutMaterialsSubdirKey = "content.layout.materials_subdir";
} // namespace

auto ContentSettingsService::GetExplorerSettings() const
  -> ContentExplorerSettings
{
  ContentExplorerSettings s;
  const auto settings = ResolveSettings();
  if (!settings)
    return s;

  if (auto val = settings->GetString(kModelRootKey))
    s.model_root = *val;
  if (auto val = settings->GetBool(kIncludeFbxKey))
    s.include_fbx = *val;
  if (auto val = settings->GetBool(kIncludeGlbKey))
    s.include_glb = *val;
  if (auto val = settings->GetBool(kIncludeGltfKey))
    s.include_gltf = *val;
  if (auto val = settings->GetBool(kAutoLoadOnImportKey))
    s.auto_load_on_import = *val;
  if (auto val = settings->GetBool(kAutoDumpTexMemKey))
    s.auto_dump_texture_memory = *val;
  if (auto val = settings->GetFloat(kAutoDumpDelayKey))
    s.auto_dump_delay_frames = static_cast<int>(*val);
  if (auto val = settings->GetFloat(kDumpTopNKey))
    s.dump_top_n = static_cast<int>(*val);

  return s;
}

auto ContentSettingsService::SetExplorerSettings(
  const ContentExplorerSettings& s) -> void
{
  if (const auto settings = ResolveSettings()) {
    settings->SetString(kModelRootKey, s.model_root.string());
    settings->SetBool(kIncludeFbxKey, s.include_fbx);
    settings->SetBool(kIncludeGlbKey, s.include_glb);
    settings->SetBool(kIncludeGltfKey, s.include_gltf);
    settings->SetBool(kAutoLoadOnImportKey, s.auto_load_on_import);
    settings->SetBool(kAutoDumpTexMemKey, s.auto_dump_texture_memory);
    settings->SetFloat(
      kAutoDumpDelayKey, static_cast<float>(s.auto_dump_delay_frames));
    settings->SetFloat(kDumpTopNKey, static_cast<float>(s.dump_top_n));
    ++epoch_;
  }
}

auto ContentSettingsService::GetImportOptions() const
  -> content::import::ImportOptions
{
  content::import::ImportOptions o;
  const auto settings = ResolveSettings();
  if (!settings)
    return o;

  if (auto val = settings->GetFloat(kAssetKeyPolicyKey))
    o.asset_key_policy
      = static_cast<content::import::AssetKeyPolicy>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kNodePruningKey))
    o.node_pruning
      = static_cast<content::import::NodePruningPolicy>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kImportContentFlagsKey))
    o.import_content = static_cast<content::import::ImportContentFlags>(
      static_cast<uint32_t>(*val));
  if (auto val = settings->GetBool(kWithHashingKey))
    o.with_content_hashing = *val;
  if (auto val = settings->GetFloat(kNormalPolicyKey))
    o.normal_policy = static_cast<content::import::GeometryAttributePolicy>(
      static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTangentPolicyKey))
    o.tangent_policy = static_cast<content::import::GeometryAttributePolicy>(
      static_cast<int>(*val));

  return o;
}

auto ContentSettingsService::SetImportOptions(
  const content::import::ImportOptions& o) -> void
{
  if (const auto settings = ResolveSettings()) {
    settings->SetFloat(kAssetKeyPolicyKey,
      static_cast<float>(static_cast<int>(o.asset_key_policy)));
    settings->SetFloat(
      kNodePruningKey, static_cast<float>(static_cast<int>(o.node_pruning)));
    settings->SetFloat(kImportContentFlagsKey,
      static_cast<float>(static_cast<uint32_t>(o.import_content)));
    settings->SetBool(kWithHashingKey, o.with_content_hashing);
    settings->SetFloat(
      kNormalPolicyKey, static_cast<float>(static_cast<int>(o.normal_policy)));
    settings->SetFloat(kTangentPolicyKey,
      static_cast<float>(static_cast<int>(o.tangent_policy)));
    ++epoch_;
  }
}

auto ContentSettingsService::GetTextureTuning() const
  -> content::import::ImportOptions::TextureTuning
{
  content::import::ImportOptions::TextureTuning t;
  const auto settings = ResolveSettings();
  if (!settings)
    return t;

  if (auto val = settings->GetBool(kTexTuningEnabledKey))
    t.enabled = *val;
  if (auto val = settings->GetFloat(kTexTuningIntentKey))
    t.intent
      = static_cast<content::import::TextureIntent>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningColorSpaceKey))
    t.source_color_space = static_cast<ColorSpace>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningMipPolicyKey))
    t.mip_policy
      = static_cast<content::import::MipPolicy>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningMaxMipsKey))
    t.max_mip_levels = static_cast<uint8_t>(*val);
  if (auto val = settings->GetFloat(kTexTuningMipFilterKey))
    t.mip_filter
      = static_cast<content::import::MipFilter>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningColorFormatKey))
    t.color_output_format = static_cast<Format>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningDataFormatKey))
    t.data_output_format = static_cast<Format>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningBc7QualityKey))
    t.bc7_quality
      = static_cast<content::import::Bc7Quality>(static_cast<int>(*val));
  if (auto val = settings->GetFloat(kTexTuningHdrHandlingKey))
    t.hdr_handling
      = static_cast<content::import::HdrHandling>(static_cast<int>(*val));

  return t;
}

auto ContentSettingsService::SetTextureTuning(
  const content::import::ImportOptions::TextureTuning& t) -> void
{
  if (const auto settings = ResolveSettings()) {
    settings->SetBool(kTexTuningEnabledKey, t.enabled);
    settings->SetFloat(
      kTexTuningIntentKey, static_cast<float>(static_cast<int>(t.intent)));
    settings->SetFloat(kTexTuningColorSpaceKey,
      static_cast<float>(static_cast<int>(t.source_color_space)));
    settings->SetFloat(kTexTuningMipPolicyKey,
      static_cast<float>(static_cast<int>(t.mip_policy)));
    settings->SetFloat(
      kTexTuningMaxMipsKey, static_cast<float>(t.max_mip_levels));
    settings->SetFloat(kTexTuningMipFilterKey,
      static_cast<float>(static_cast<int>(t.mip_filter)));
    settings->SetFloat(kTexTuningColorFormatKey,
      static_cast<float>(static_cast<int>(t.color_output_format)));
    settings->SetFloat(kTexTuningDataFormatKey,
      static_cast<float>(static_cast<int>(t.data_output_format)));
    settings->SetFloat(kTexTuningBc7QualityKey,
      static_cast<float>(static_cast<int>(t.bc7_quality)));
    settings->SetFloat(kTexTuningHdrHandlingKey,
      static_cast<float>(static_cast<int>(t.hdr_handling)));
    ++epoch_;
  }
}

auto ContentSettingsService::GetDefaultLayout() const
  -> content::import::LooseCookedLayout
{
  content::import::LooseCookedLayout l;
  const auto settings = ResolveSettings();
  if (!settings)
    return l;

  if (auto val = settings->GetString(kLayoutVirtualRootKey))
    l.virtual_mount_root = *val;
  if (auto val = settings->GetString(kLayoutIndexNameKey))
    l.index_file_name = *val;
  if (auto val = settings->GetString(kLayoutResourcesDirKey))
    l.resources_dir = *val;
  if (auto val = settings->GetString(kLayoutDescriptorsDirKey))
    l.descriptors_dir = *val;
  if (auto val = settings->GetString(kLayoutScenesSubdirKey))
    l.scenes_subdir = *val;
  if (auto val = settings->GetString(kLayoutGeometrySubdirKey))
    l.geometry_subdir = *val;
  if (auto val = settings->GetString(kLayoutMaterialsSubdirKey))
    l.materials_subdir = *val;

  return l;
}

auto ContentSettingsService::SetDefaultLayout(
  const content::import::LooseCookedLayout& l) -> void
{
  if (const auto settings = ResolveSettings()) {
    settings->SetString(kLayoutVirtualRootKey, l.virtual_mount_root);
    settings->SetString(kLayoutIndexNameKey, l.index_file_name);
    settings->SetString(kLayoutResourcesDirKey, l.resources_dir);
    settings->SetString(kLayoutDescriptorsDirKey, l.descriptors_dir);
    settings->SetString(kLayoutScenesSubdirKey, l.scenes_subdir);
    settings->SetString(kLayoutGeometrySubdirKey, l.geometry_subdir);
    settings->SetString(kLayoutMaterialsSubdirKey, l.materials_subdir);
    ++epoch_;
  }
}

auto ContentSettingsService::GetLastCookedOutputDirectory() const -> std::string
{
  const auto settings = ResolveSettings();
  return settings ? settings->GetString(kLastCookedOutputKey).value_or("") : "";
}

auto ContentSettingsService::SetLastCookedOutputDirectory(
  const std::string& path) -> void
{
  if (const auto settings = ResolveSettings()) {
    settings->SetString(kLastCookedOutputKey, path);
    ++epoch_;
  }
}

auto ContentSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto ContentSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples
