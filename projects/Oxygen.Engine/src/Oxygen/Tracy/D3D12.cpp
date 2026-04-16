//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Tracy/D3D12.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <tracy/Tracy.hpp>
#include <tracy/TracyD3D12.hpp>

namespace oxygen::tracy::d3d12 {
namespace {

  struct SourceLocationKey {
    uint32_t line { 0U };
    uint32_t color { 0U };
    std::string source {};
    std::string function {};
    std::string name {};

    auto operator==(const SourceLocationKey&) const -> bool = default;
  };

  struct SourceLocationKeyHash {
    auto operator()(const SourceLocationKey& key) const noexcept -> std::size_t
    {
      auto hash = std::hash<uint32_t> {}(key.line);
      hash ^= std::hash<uint32_t> {}(key.color) + 0x9E3779B9U + (hash << 6U)
        + (hash >> 2U);
      hash ^= std::hash<std::string> {}(key.source) + 0x9E3779B9U + (hash << 6U)
        + (hash >> 2U);
      hash ^= std::hash<std::string> {}(key.function) + 0x9E3779B9U
        + (hash << 6U) + (hash >> 2U);
      hash ^= std::hash<std::string> {}(key.name) + 0x9E3779B9U + (hash << 6U)
        + (hash >> 2U);
      return hash;
    }
  };

  auto SourceLocationCache()
    -> std::unordered_map<SourceLocationKey, ::tracy::SourceLocationData,
      SourceLocationKeyHash>&
  {
    static auto cache = std::unordered_map<SourceLocationKey, ::tracy::SourceLocationData,
      SourceLocationKeyHash> {};
    return cache;
  }

  auto SourceLocationCacheMutex() -> std::mutex&
  {
    static auto mutex = std::mutex {};
    return mutex;
  }

  auto GetOrCreateSourceLocation(
    const std::source_location callsite, const std::string_view stable_name)
    -> const ::tracy::SourceLocationData*
  {
    auto key = SourceLocationKey {
      .line = static_cast<uint32_t>(callsite.line()),
      .color = 0U,
      .source = callsite.file_name(),
      .function = callsite.function_name(),
      .name = std::string(stable_name),
    };

    auto& cache = SourceLocationCache();
    std::lock_guard lock(SourceLocationCacheMutex());
    if (const auto it = cache.find(key); it != cache.end()) {
      return &it->second;
    }

    const auto [it, inserted] = cache.try_emplace(key);
    static_cast<void>(inserted);
    it->second = ::tracy::SourceLocationData {
      .name = it->first.name.empty() ? nullptr : it->first.name.c_str(),
      .function = it->first.function.c_str(),
      .file = it->first.source.c_str(),
      .line = it->first.line,
      .color = it->first.color,
    };
    return &it->second;
  }

  auto ToContext(const ContextHandle context) -> TracyD3D12Ctx
  {
    return static_cast<TracyD3D12Ctx>(context);
  }

} // namespace

auto CreateContext(ID3D12Device* device, ID3D12CommandQueue* queue)
  -> ContextHandle
{
  if (device == nullptr || queue == nullptr) {
    return nullptr;
  }
  return ::tracy::CreateD3D12Context(device, queue);
}

auto DestroyContext(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ::tracy::DestroyD3D12Context(ToContext(context));
  }
}

auto AdvanceContextFrame(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ToContext(context)->NewFrame();
  }
}

auto CollectContext(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ToContext(context)->Collect();
  }
}

auto NameContext(const ContextHandle context, const std::string_view name)
  -> void
{
  if (context != nullptr) {
    ToContext(context)->Name(name.data(), static_cast<uint16_t>(name.size()));
  }
}

auto BeginZone(const std::span<std::byte> storage, const ContextHandle context,
  ID3D12GraphicsCommandList* command_list, const std::source_location callsite,
  const std::string_view name) -> bool
{
  using ::tracy::D3D12ZoneScope;

  if (context == nullptr || command_list == nullptr
    || storage.size_bytes() < sizeof(D3D12ZoneScope)) {
    return false;
  }

  const auto* source_location = GetOrCreateSourceLocation(callsite, name);
  std::construct_at(reinterpret_cast<D3D12ZoneScope*>(storage.data()),
    ToContext(context), command_list, source_location, true);
  return true;
}

auto EndZone(const std::span<std::byte> storage) -> void
{
  using ::tracy::D3D12ZoneScope;

  if (storage.size_bytes() < sizeof(D3D12ZoneScope)) {
    return;
  }

  std::destroy_at(reinterpret_cast<D3D12ZoneScope*>(storage.data()));
}

auto CachedSourceLocationCountForTesting() -> std::size_t
{
  std::lock_guard lock(SourceLocationCacheMutex());
  return SourceLocationCache().size();
}

} // namespace oxygen::tracy::d3d12
