//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Console/Completion.h>

namespace oxygen::console {

auto to_string(const CompletionKind value) -> const char*
{
  switch (value) {
  case CompletionKind::kCommand:
    return "Command";
  case CompletionKind::kCVar:
    return "CVar";
  }
  return "__NotSupported__";
}

void CompletionCycle::Reset()
{
  prefix_.clear();
  candidates_.clear();
  index_ = kCompletionCycleStartIndex;
}

void CompletionCycle::Begin(
  const std::string_view prefix, std::vector<CompletionCandidate> candidates)
{
  prefix_ = std::string(prefix);
  candidates_ = std::move(candidates);
  index_ = kCompletionCycleStartIndex;
}

auto CompletionCycle::Current() const -> observer_ptr<const CompletionCandidate>
{
  if (candidates_.empty() || index_ >= candidates_.size()) {
    return observer_ptr<const CompletionCandidate> {};
  }
  return make_observer(&candidates_[index_]);
}

auto CompletionCycle::Next() -> observer_ptr<const CompletionCandidate>
{
  if (candidates_.empty()) {
    return observer_ptr<const CompletionCandidate> {};
  }

  index_ = (index_ + 1) % candidates_.size();
  return make_observer(&candidates_[index_]);
}

auto CompletionCycle::Previous() -> observer_ptr<const CompletionCandidate>
{
  if (candidates_.empty()) {
    return observer_ptr<const CompletionCandidate> {};
  }

  if (index_ == 0) {
    index_ = candidates_.size() - 1;
  } else {
    --index_;
  }
  return make_observer(&candidates_[index_]);
}

auto CompletionCycle::IsActive() const noexcept -> bool
{
  return !candidates_.empty();
}

auto CompletionCycle::Prefix() const -> std::string_view { return prefix_; }

} // namespace oxygen::console
