//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Tracy/Cpu.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

extern "C" {
#include <tracy/TracyC.h>
}

namespace oxygen::tracy::cpu {
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
    -> std::unordered_map<SourceLocationKey, ___tracy_source_location_data,
      SourceLocationKeyHash>&
  {
    static auto cache = std::unordered_map<SourceLocationKey, ___tracy_source_location_data,
      SourceLocationKeyHash> {};
    return cache;
  }

  auto SourceLocationCacheMutex() -> std::mutex&
  {
    static auto mutex = std::mutex {};
    return mutex;
  }

  auto GetOrCreateSourceLocation(const std::source_location callsite,
    const std::string_view stable_name, const uint32_t color_rgb24)
    -> const ___tracy_source_location_data*
  {
    auto key = SourceLocationKey {
      .line = static_cast<uint32_t>(callsite.line()),
      .color = color_rgb24,
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
    it->second = ___tracy_source_location_data {
      .name = it->first.name.empty() ? nullptr : it->first.name.c_str(),
      .function = it->first.function.c_str(),
      .file = it->first.source.c_str(),
      .line = it->first.line,
      .color = it->first.color,
    };
    return &it->second;
  }

} // namespace

auto BeginZone(const std::span<std::byte> storage,
  const std::source_location callsite, const std::string_view stable_name,
  const uint32_t color_rgb24) -> bool
{
  if (storage.size_bytes() < sizeof(TracyCZoneCtx)) {
    return false;
  }

  const auto* source_location
    = GetOrCreateSourceLocation(callsite, stable_name, color_rgb24);
  const auto tracy_ctx = ___tracy_emit_zone_begin(source_location, 1);
  std::memcpy(storage.data(), &tracy_ctx, sizeof(tracy_ctx));
  return true;
}

auto EndZone(const std::span<const std::byte> storage) -> void
{
  if (storage.size_bytes() < sizeof(TracyCZoneCtx)) {
    return;
  }

  TracyCZoneCtx tracy_ctx {};
  std::memcpy(&tracy_ctx, storage.data(), sizeof(tracy_ctx));
  ___tracy_emit_zone_end(tracy_ctx);
}

auto CachedSourceLocationCountForTesting() -> std::size_t
{
  std::lock_guard lock(SourceLocationCacheMutex());
  return SourceLocationCache().size();
}

} // namespace oxygen::tracy::cpu
