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
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/LoaderFunction.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/data/AssetKey.h>
#include <Oxygen/data/AssetType.h>

namespace oxygen::content {

class AssetLoader {
public:
  OXGN_CNTT_API AssetLoader();
  ~AssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  // RegisterLoader now requires loader functions to return shared_ptr<T>
  template <LoaderFunction F> void RegisterLoader(F&& fn)
  {
    using LoaderPtr = decltype(fn(
      std::declval<oxygen::serio::Reader<oxygen::serio::FileStream<>>>()));
    using LoaderType = std::remove_pointer_t<typename LoaderPtr::element_type>;
    static_assert(IsTyped<LoaderType>, "LoaderType must satisfy HasTypeInfo");

    auto type_id = LoaderType::ClassTypeId();
    auto type_name = LoaderType::ClassTypeNamePretty();

    LoaderFnErased erased
      = [fn = std::forward<F>(fn)](const PakFile& pak,
          const oxygen::data::pak::AssetDirectoryEntry& entry)
      -> std::shared_ptr<void> {
      auto reader = pak.CreateReader(entry);
      auto uptr = fn(std::move(reader));
      return uptr ? std::shared_ptr<void>(std::move(uptr)) : nullptr;
    };
    AddTypeErasedLoader(type_id, type_name, std::move(erased));
  }

  OXGN_CNTT_API void AddPakFile(const std::filesystem::path& path);

  // Load returns shared_ptr<T> and uses static_pointer_cast for type safety
  template <IsTyped T>
  std::shared_ptr<T> Load(const oxygen::data::AssetKey& key)
  {
    for (const auto& pak : paks_) {
      auto entry_opt = pak->FindEntry(key);
      if (entry_opt) {
        const auto& entry = *entry_opt;
        auto loader_it = loaders_.find(T::ClassTypeId());
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
    const PakFile&, const oxygen::data::pak::AssetDirectoryEntry&)>;

  std::unordered_map<oxygen::TypeId, LoaderFnErased> loaders_;

  OXGN_CNTT_API void AddTypeErasedLoader(
    oxygen::TypeId type_id, std::string_view type_name, LoaderFnErased loader);

  std::vector<std::unique_ptr<PakFile>> paks_;
};

} // namespace oxygen::content
