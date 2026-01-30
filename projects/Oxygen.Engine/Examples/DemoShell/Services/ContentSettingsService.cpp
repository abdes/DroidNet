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
  // Texture tuning is complex, for now we return defaults
  // or add specific fields if needed.
  return {};
}

auto ContentSettingsService::SetTextureTuning(
  const content::import::ImportOptions::TextureTuning& tuning) -> void
{
  // Placeholder for complex settings
  (void)tuning;
  ++epoch_;
}

auto ContentSettingsService::GetDefaultLayout() const
  -> content::import::LooseCookedLayout
{
  return {};
}

auto ContentSettingsService::SetDefaultLayout(
  const content::import::LooseCookedLayout& layout) -> void
{
  (void)layout;
  ++epoch_;
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
