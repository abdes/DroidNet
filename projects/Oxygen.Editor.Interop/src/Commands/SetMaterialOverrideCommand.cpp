//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <string>
#include <string_view>

#include <Commands/SetMaterialOverrideCommand.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace {

auto NormalizeMaterialVirtualPath(std::string_view material_uri) -> std::string
{
  if (material_uri.empty()) {
    return {};
  }

  if (material_uri.starts_with("asset:")) {
    material_uri.remove_prefix(6);
  }

  while (!material_uri.empty() && material_uri.front() == '/') {
    material_uri.remove_prefix(1);
  }

  auto virtual_path = "/" + std::string(material_uri);
  constexpr std::string_view descriptor_suffix = ".omat.json";
  if (virtual_path.ends_with(descriptor_suffix)) {
    virtual_path.resize(virtual_path.size() - std::string(".json").size());
  }

  return virtual_path;
}

} // namespace

namespace oxygen::interop::module {

  void SetMaterialOverrideCommand::Execute(CommandContext& context)
  {
    if (!context.Scene) {
      return;
    }

    const auto scene_node_opt = context.Scene->GetNode(node_);
    if (!scene_node_opt || !scene_node_opt->IsAlive()) {
      return;
    }

    auto scene_node = *scene_node_opt;
    constexpr std::size_t lod_index = 0;

    if (material_uri_.empty()) {
      scene_node.GetRenderable().ClearMaterialOverride(lod_index, slot_index_);
      LOG_F(INFO,
        "SetMaterialOverrideCommand: cleared override slot {} on node",
        slot_index_);
      return;
    }

    if (!context.PathResolver || !context.AssetLoader) {
      LOG_F(WARNING,
        "SetMaterialOverrideCommand: missing resolver/loader for '{}'",
        material_uri_);
      return;
    }

    const auto virtual_path = NormalizeMaterialVirtualPath(material_uri_);
    LOG_F(INFO,
      "SetMaterialOverrideCommand: resolving material virtual path '{}'",
      virtual_path);

    auto key = context.PathResolver->ResolveAssetKey(virtual_path);
    if (!key) {
      LOG_F(WARNING,
        "SetMaterialOverrideCommand: could not resolve material key for '{}'",
        virtual_path);
      return;
    }

    auto material = context.AssetLoader->GetMaterialAsset(*key);
    if (material) {
      scene_node.GetRenderable().SetMaterialOverride(
        lod_index, slot_index_, std::move(material));
      LOG_F(INFO,
        "SetMaterialOverrideCommand: applied cached material '{}' to slot {}",
        virtual_path, slot_index_);
      return;
    }

    const auto asset_key = *key;
    const auto node = scene_node;
    const auto slot_index = slot_index_;
    context.AssetLoader->StartLoadMaterialAsset(
      asset_key,
      [node, slot_index, virtual_path](
        std::shared_ptr<oxygen::data::MaterialAsset> loaded) {
        if (!loaded) {
          LOG_F(ERROR,
            "SetMaterialOverrideCommand: async material load failed for '{}'",
            virtual_path);
          return;
        }

        if (!node.IsAlive()) {
          LOG_F(WARNING,
            "SetMaterialOverrideCommand: node no longer alive; skipping '{}'",
            virtual_path);
          return;
        }

        constexpr std::size_t lod_index = 0;
        node.GetRenderable().SetMaterialOverride(
          lod_index, slot_index, std::move(loaded));
        LOG_F(INFO,
          "SetMaterialOverrideCommand: applied async material '{}' to slot {}",
          virtual_path, slot_index);
      });
  }

} // namespace oxygen::interop::module
