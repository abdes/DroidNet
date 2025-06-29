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
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

// Concept: T must derive from oxygen::Object and have static ClassTypeId()
// returning oxygen::TypeId
template <typename T>
concept HasTypeInfo = std::derived_from<T, oxygen::Object> && requires {
  { T::ClassTypeId() } -> std::same_as<oxygen::TypeId>;
};

// Concept: LoaderFn must return a unique_ptr<XXX> where XXX is HasTypeInfo and
// accept (const PakFile&, const AssetDirectoryEntry&)
template <typename F>
concept LoaderFunction = requires(
  F f, const PakFile& pak, const AssetDirectoryEntry& entry) {
  typename std::remove_cvref_t<decltype(*f(pak, entry))>;
  requires HasTypeInfo<std::remove_pointer_t<decltype(f(pak, entry).get())>>;
  { f(pak, entry) };
};

class AssetLoader {
public:
  OXGN_CNTT_API AssetLoader();
  ~AssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  // RegisterLoader now requires loader functions to return shared_ptr<T>
  template <LoaderFunction F> void RegisterLoader(AssetType type, F&& fn)
  {
    LoaderFnErased erased
      = [fn = std::forward<F>(fn)](const PakFile& pak,
          const AssetDirectoryEntry& entry) -> std::shared_ptr<void> {
      auto uptr = fn(pak, entry);
      return uptr ? std::shared_ptr<void>(std::move(uptr)) : nullptr;
    };
    AddTypeErasedLoader(type, std::move(erased));
  }

  OXGN_CNTT_API void AddPakFile(const std::filesystem::path& path);

  // Load returns shared_ptr<T> and uses static_pointer_cast for type safety
  template <HasTypeInfo T> std::shared_ptr<T> Load(const AssetKey& key)
  {
    for (const auto& pak : paks_) {
      auto entry_opt = pak->FindEntry(key);
      if (entry_opt) {
        const auto& entry = *entry_opt;
        auto loader_it = loaders_.find(static_cast<AssetType>(key.type));
        if (loader_it != loaders_.end()) {
          auto void_ptr = loader_it->second(*pak, entry);
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

private:
  // Use shared_ptr<void> for type erasure
  using LoaderFnErased = std::function<std::shared_ptr<void>(
    const PakFile&, const AssetDirectoryEntry&)>;

  std::unordered_map<AssetType, LoaderFnErased> loaders_;

  OXGN_CNTT_API void AddTypeErasedLoader(AssetType type, LoaderFnErased loader);

  std::vector<std::unique_ptr<PakFile>> paks_;
};

} // namespace oxygen::content
