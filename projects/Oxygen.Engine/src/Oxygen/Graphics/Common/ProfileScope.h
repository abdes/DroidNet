//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandRecorder;

struct GpuEventScopeOptions {
  bool timestamp_enabled = false;
};

struct GpuEventScopeToken {
  uint32_t scope_id { 0 };
  uint16_t stream_id { 0 };
  uint8_t flags { 0 };
};

class IGpuProfileScopeHandler {
public:
  OXGN_GFX_API virtual ~IGpuProfileScopeHandler();

  [[nodiscard]] virtual auto BeginScope(CommandRecorder& recorder,
    std::string_view name, const GpuEventScopeOptions& options)
    -> GpuEventScopeToken
    = 0;

  virtual auto EndScope(
    CommandRecorder& recorder, const GpuEventScopeToken& token) -> void
    = 0;
};

inline constexpr uint8_t kGpuScopeTokenFlagTimestampEnabled = 1U << 0U;
inline constexpr uint8_t kGpuScopeTokenFlagOverflowAtCreation = 1U << 1U;

} // namespace oxygen::graphics
