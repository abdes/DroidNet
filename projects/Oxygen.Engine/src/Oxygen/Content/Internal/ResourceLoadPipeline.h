//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::internal {

class ResourceLoadPipeline final {
public:
  using ContentCache = AnyCache<uint64_t, RefCountedEviction<uint64_t>>;
  using ResourceLoadFn = std::function<std::shared_ptr<void>(LoaderContext)>;
  using ResourceLoaderMap = std::unordered_map<TypeId, ResourceLoadFn>;

  struct Callbacks final {
    std::function<void()> assert_owning_thread;
    std::function<uint64_t(const ResourceKey&)> hash_resource_key;
    std::function<void(uint64_t, ResourceKey)> map_resource_key;
    std::function<LoadPriorityClass()> default_priority_class;
    std::function<uint64_t()> next_request_sequence;
  };

  ResourceLoadPipeline(const ContentSourceRegistry& source_registry,
    const ResourceLoaderMap& resource_loaders, ContentCache& content_cache,
    InFlightOperationTable& in_flight_ops,
    observer_ptr<co::ThreadPool> thread_pool, bool work_offline,
    Callbacks callbacks);

  auto LoadErased(TypeId resource_type, ResourceKey key)
    -> co::Co<std::shared_ptr<void>>;
  auto LoadErased(TypeId resource_type, ResourceKey key,
    const LoadRequest& request) -> co::Co<std::shared_ptr<void>>;

  auto LoadErasedFromCooked(TypeId resource_type, ResourceKey key,
    std::span<const uint8_t> bytes) -> co::Co<std::shared_ptr<void>>;
  auto LoadErasedFromCooked(TypeId resource_type, ResourceKey key,
    std::span<const uint8_t> bytes, const LoadRequest& request)
    -> co::Co<std::shared_ptr<void>>;

private:
  const ContentSourceRegistry& source_registry_;
  const ResourceLoaderMap& resource_loaders_;
  ContentCache& content_cache_;
  InFlightOperationTable& in_flight_ops_;
  observer_ptr<co::ThreadPool> thread_pool_;
  bool work_offline_ { false };
  Callbacks callbacks_;
};

} // namespace oxygen::content::internal
