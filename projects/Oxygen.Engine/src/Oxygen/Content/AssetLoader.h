//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/data/AssetKey.h>
#include <Oxygen/data/AssetType.h>

namespace oxygen::content {

class AssetLoader {
public:
  OXGN_CNTT_API AssetLoader();
  virtual ~AssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  // RegisterLoader now uses the unified LoaderContext API
  template <LoadFunction F> void RegisterLoader(F&& fn)
  {
    using LoaderPtr = decltype(fn(
      std::declval<LoaderContext<oxygen::serio::FileStream<>>>()));
    using LoaderType = std::remove_pointer_t<typename LoaderPtr::element_type>;
    static_assert(IsTyped<LoaderType>, "LoaderType must satisfy HasTypeInfo");

    auto type_id = LoaderType::ClassTypeId();
    auto type_name = LoaderType::ClassTypeNamePretty();

    LoaderFnErased erased
      = [fn = std::forward<F>(fn)](AssetLoader& loader, const PakFile& pak,
          const oxygen::data::pak::AssetDirectoryEntry& entry,
          bool offline) -> std::shared_ptr<void> {
      auto reader = pak.CreateReader(entry);
      LoaderContext<oxygen::serio::FileStream<>> context { .asset_loader
        = &loader,
        .current_asset_key = entry.asset_key,
        .reader = std::ref(reader),
        .offline = offline };
      auto uptr = fn(context);
      return uptr ? std::shared_ptr<void>(std::move(uptr)) : nullptr;
    };
    AddTypeErasedLoader(type_id, type_name, std::move(erased));
  }

  OXGN_CNTT_API void AddPakFile(const std::filesystem::path& path);

  //=== Dependency Management
  //===---------------------------------------------------//

  //! Register an asset-to-asset dependency
  /*!
   Records that an asset depends on another asset for proper loading order
   and reference counting.

   @param dependent The asset that has the dependency
   @param dependency The asset that is depended upon
   */
  OXGN_CNTT_API virtual void AddAssetDependency(
    const oxygen::data::AssetKey& dependent,
    const oxygen::data::AssetKey& dependency);

  //! Register an asset-to-resource dependency
  /*!
   Records that an asset depends on a resource for proper loading order
   and reference counting.

   @param dependent The asset that has the dependency
   @param resource_index The resource index that is depended upon
   */
  OXGN_CNTT_API virtual void AddResourceDependency(
    const oxygen::data::AssetKey& dependent,
    oxygen::data::pak::ResourceIndexT resource_index);

  //=== Asset Loading
  //===-----------------------------------------------------------//

  // Load returns shared_ptr<T> and uses static_pointer_cast for type safety
  template <IsTyped T>
  std::shared_ptr<T> Load(const oxygen::data::AssetKey& key)
  {
    return LoadImpl<T>(key, false);
  }

  // Load in offline mode (no GPU side effects)
  template <IsTyped T>
  std::shared_ptr<T> LoadOffline(const oxygen::data::AssetKey& key)
  {
    return LoadImpl<T>(key, true);
  }

private:
  // Internal implementation for loading with offline parameter
  template <IsTyped T>
  std::shared_ptr<T> LoadImpl(const oxygen::data::AssetKey& key, bool offline)
  {
    for (const auto& pak : paks_) {
      auto entry_opt = pak->FindEntry(key);
      if (entry_opt) {
        const auto& entry = *entry_opt;
        auto loader_it = loaders_.find(T::ClassTypeId());
        if (loader_it != loaders_.end()) {
          auto void_ptr = loader_it->second(*this, *pak, entry, offline);
          auto typed = std::static_pointer_cast<T>(void_ptr);
          if (typed && typed->GetTypeId() == T::ClassTypeId()) {
            return typed;
          }
        }
        return nullptr;
      }
    }
    return nullptr;
  }
  // Use shared_ptr<void> for type erasure, loader functions get AssetLoader&
  // reference
  using LoaderFnErased = std::function<std::shared_ptr<void>(AssetLoader&,
    const PakFile&, const oxygen::data::pak::AssetDirectoryEntry&, bool)>;

  std::unordered_map<oxygen::TypeId, LoaderFnErased> loaders_;

  //=== Dependency Tracking
  //===----------------------------------------------------//

  // Asset-to-asset dependencies: dependent_asset -> set of dependency_assets
  std::unordered_map<oxygen::data::AssetKey,
    std::unordered_set<oxygen::data::AssetKey>>
    asset_dependencies_;

  // Asset-to-resource dependencies: dependent_asset -> set of resource_indices
  std::unordered_map<oxygen::data::AssetKey,
    std::unordered_set<oxygen::data::pak::ResourceIndexT>>
    resource_dependencies_;

  // Reverse mapping: dependency_asset -> set of dependent_assets (for reference
  // counting)
  std::unordered_map<oxygen::data::AssetKey,
    std::unordered_set<oxygen::data::AssetKey>>
    reverse_asset_dependencies_;

  // Reverse mapping: resource_index -> set of dependent_assets (for reference
  // counting)
  std::unordered_map<oxygen::data::pak::ResourceIndexT,
    std::unordered_set<oxygen::data::AssetKey>>
    reverse_resource_dependencies_;

  //=== Implementation
  //===----------------------------------------------------------//

  OXGN_CNTT_API void AddTypeErasedLoader(
    oxygen::TypeId type_id, std::string_view type_name, LoaderFnErased loader);

  std::vector<std::unique_ptr<PakFile>> paks_;
};

} // namespace oxygen::content
