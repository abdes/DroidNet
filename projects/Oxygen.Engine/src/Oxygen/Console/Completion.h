//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen::console {

enum class CompletionKind : uint8_t {
  kCommand,
  kCVar,
};

struct CompletionCandidate {
  CompletionKind kind { CompletionKind::kCommand };
  std::string token;
  std::string help;
};

struct ConsoleSymbol {
  CompletionKind kind { CompletionKind::kCommand };
  std::string token;
  std::string help;
  uint64_t usage_frequency { 0 };
  uint64_t usage_last_tick { 0 };
};

OXGN_CONS_NDAPI auto to_string(CompletionKind value) -> const char*;

class CompletionCycle final {
public:
  OXGN_CONS_API CompletionCycle() = default;
  ~CompletionCycle() = default;

  OXYGEN_DEFAULT_COPYABLE(CompletionCycle)
  OXYGEN_DEFAULT_MOVABLE(CompletionCycle)

  OXGN_CONS_API void Reset();
  OXGN_CONS_API void Begin(
    std::string_view prefix, std::vector<CompletionCandidate> candidates);
  OXGN_CONS_NDAPI auto Current() const
    -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto Next() -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto Previous() -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto IsActive() const noexcept -> bool;
  OXGN_CONS_NDAPI auto Prefix() const -> std::string_view;

private:
  std::string prefix_;
  std::vector<CompletionCandidate> candidates_;
  size_t index_ { kCompletionCycleStartIndex };
};

} // namespace oxygen::console
