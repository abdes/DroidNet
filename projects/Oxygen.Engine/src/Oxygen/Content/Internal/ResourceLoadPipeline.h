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
#include <string_view>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
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
    std::function<void(ResourceKey)> on_resource_published;
    std::function<LoadPriorityClass()> default_priority_class;
    std::function<uint64_t()> next_request_sequence;
    std::function<void(TypeId)> on_resource_request;
    std::function<void(TypeId)> on_resource_cache_hit;
    std::function<void(TypeId)> on_resource_cache_miss;
    std::function<void(TypeId)> on_resource_joined_inflight;
    std::function<void(TypeId)> on_resource_started_inflight;
    std::function<void(TypeId)> on_resource_decode_failure;
    std::function<void(TypeId)> on_resource_type_mismatch;
    std::function<void(TypeId)> on_resource_store_retry {};
    std::function<void(TypeId)> on_resource_store_retry_failed {};
    std::function<void(std::string_view, bool)> on_store_pressure;
  };

  ResourceLoadPipeline(const ContentSourceRegistry& source_registry,
    const ContentIdentityRegistry& identities,
    const ResourceLoaderMap& resource_loaders, ContentCache& content_cache,
    InFlightOperationTable& in_flight_ops,
    const std::shared_ptr<ContentReleaseQueue>& releases,
    observer_ptr<co::ThreadPool> thread_pool, bool work_offline,
    Callbacks callbacks);

  auto LoadErased(TypeId resource_type, ResourceKey key,
    LoadRequest request = {}, CheckoutOwner owner = CheckoutOwner::kExternal)
    -> co::Co<ContentAcquisition>;

  auto LoadErasedFromCooked(TypeId resource_type, ResourceKey key,
    std::span<const uint8_t> bytes, LoadRequest request = {},
    CheckoutOwner owner = CheckoutOwner::kExternal)
    -> co::Co<ContentAcquisition>;

private:
  const ContentSourceRegistry& source_registry_;
  const ContentIdentityRegistry& identities_;
  const ResourceLoaderMap& resource_loaders_;
  ContentCache& content_cache_;
  InFlightOperationTable& in_flight_ops_;
  const std::shared_ptr<ContentReleaseQueue>& releases_;
  observer_ptr<co::ThreadPool> thread_pool_;
  bool work_offline_ { false };
  Callbacks callbacks_;
};

} // namespace oxygen::content::internal
