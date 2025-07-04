//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content {

template <PakResource T, oxygen::serio::Stream S> class ResourceTable {
public:
  using DescT = typename T::DescT;
  using ReaderT = oxygen::serio::Reader<std::remove_reference_t<S>>;
  using ResourceKeyT = data::pak::ResourceIndexT;

  using LoaderFnErased = std::function<std::shared_ptr<T>(
    LoaderContext<std::remove_reference_t<S>>)>;

  //! Construct with custom loader and resource table metadata
  /*!
   Constructs a ResourceTable using the provided stream, loader function, and
   resource table metadata (from PakFormat.h). The resource table metadata
   describes the offset, count, and entry size of the resource table in the PAK
   file.

   @param stream      Unique pointer to the stream containing the resource data.
   @param loader_fn   Loader function for resource type T.
   @param table_meta  ResourceTable metadata struct (PakFormat.h).

   @note The stream is seeked to the offset specified in table_meta before
   reading.
   @note The entry_size in table_meta must match sizeof(ResourceKeyT) +
   sizeof(DescT).
   @throw std::invalid_argument if entry_size does not match expected size.
  */
  template <typename LoaderFn>
    requires LoadFunctionForStream<LoaderFn, std::remove_reference_t<S>>
  ResourceTable(std::unique_ptr<S> stream,
    const data::pak::ResourceTable& table_meta, LoaderFn loader_fn)
    : stream_(std::move(stream))
    , reader_(*stream_)
    , loader_(MakeTypeErasedLoader(std::move(loader_fn)))
    , table_meta_(table_meta)
  {
    // Validate entry size or abort
    constexpr std::size_t kExpectedEntrySize = sizeof(DescT);
    CHECK_EQ_F(table_meta.entry_size, kExpectedEntrySize,
      "ResourceTable: entry_size does not match expected size");
  }

  //! Returns the resource if loaded, does not load if missing
  auto GetResource(const ResourceKeyT& key) const noexcept -> std::shared_ptr<T>
  {
    auto it = resources_.find(key);
    if (it != resources_.end()) {
      return it->second;
    }
    return {};
  }

  //! Returns true if the resource is loaded (in cache)
  auto HasResource(const ResourceKeyT& key) const noexcept -> bool
  {
    return resources_.find(key) != resources_.end();
  }

  //! Find or lazily create resource by key
  auto GetOrLoadResource(const ResourceKeyT& key, bool offline = false)
    -> std::shared_ptr<T>
  {

    if (!IsValidKey(key)) {
      return {};
    }

    auto it = resources_.find(key);
    if (it != resources_.end()) {
      return it->second;
    }

    if (!stream_->seek(OffsetForKey(key))) {
      return {};
    }

    // Load resource from the descriptor at that position
    LoaderContext<std::remove_reference_t<S>> context { .asset_loader
      = nullptr, // Resource loaders don't have asset_loader
      .current_asset_key = {}, // No asset key for resources
      .reader = std::ref(reader_),
      .offline = offline };
    std::shared_ptr<T> res = loader_(context);
    resources_.emplace(key, res);
    return res;
  }

  //! Remove a resource from the cache (unload)
  void OnResourceUnloaded(const ResourceKeyT& key) { resources_.erase(key); }

  //! Returns the number of resources described in the table
  auto Size() const noexcept -> std::size_t { return table_meta_.count; }

private:
  // Helper to type-erase a loader function, enforcing
  // LoadFunctionForStream concept
  template <typename LoaderFn>
  static LoaderFnErased MakeTypeErasedLoader(LoaderFn&& fn)
  {
    return [fn = std::forward<LoaderFn>(fn)](
             LoaderContext<std::remove_reference_t<S>> context)
             -> std::shared_ptr<T> { return fn(context); };
  }

  // Helper: for index-based keys, just return the key
  static constexpr size_t IndexForKey(ResourceKeyT key) noexcept
  {
    return static_cast<size_t>(key);
  }

  // Helper: compute the absolute offset for a given key
  uint64_t OffsetForKey(ResourceKeyT key) const noexcept
  {
    return table_meta_.offset
      + static_cast<uint64_t>(IndexForKey(key)) * table_meta_.entry_size;
  }

  // Helper: check if key is valid (in range)
  bool IsValidKey(ResourceKeyT key) const noexcept
  {
    return key < table_meta_.count;
  }

  std::unique_ptr<S> stream_;
  ReaderT reader_;
  std::unordered_map<ResourceKeyT, std::shared_ptr<T>> resources_;
  LoaderFnErased loader_;
  data::pak::ResourceTable table_meta_;
};

} // namespace oxygen::content
