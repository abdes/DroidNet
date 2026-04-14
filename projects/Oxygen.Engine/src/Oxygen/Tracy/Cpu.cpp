//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Tracy/Cpu.h>

#include <cstring>

extern "C" {
#include <tracy/TracyC.h>
}

namespace oxygen::tracy::cpu {

auto BeginZone(const std::span<std::byte> storage,
  const std::source_location callsite, const std::string_view name,
  const uint32_t color_rgb24) -> bool
{
  if (storage.size_bytes() < sizeof(TracyCZoneCtx)) {
    return false;
  }

  const auto source_location
    = ___tracy_alloc_srcloc_name(static_cast<uint32_t>(callsite.line()),
      callsite.file_name(), std::strlen(callsite.file_name()),
      callsite.function_name(), std::strlen(callsite.function_name()),
      name.data(), name.size(), color_rgb24);
  const auto tracy_ctx = ___tracy_emit_zone_begin_alloc(source_location, 1);
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

} // namespace oxygen::tracy::cpu
