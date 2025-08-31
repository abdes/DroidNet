//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Headless/CommandContext.h>
#include <Oxygen/Graphics/Headless/Commands/ResourceBarrierCommand.h>

using namespace oxygen::graphics;
using namespace oxygen::graphics::headless;

auto ResourceBarrierCommand::DoExecute(CommandContext& ctx) -> void
{
  if (ctx.IsCancelled()) {
    DLOG_F(4, "ResourceBarrierCommand::DoExecute cancelled");
    return;
  }

  DLOG_F(3, "barriers   : {}", barriers_.size());

  // Basic validation: ensure 'after' is not unknown
  for (auto& b : barriers_) {
    const auto before = b.GetStateBefore();
    const auto after = b.GetStateAfter();
    DLOG_F(3, "resource   : {}", b.GetResource());
    DLOG_F(3, "transition : {} -> {}", before, after);
  }

  // Apply barriers into the headless observed state map provided by the
  // CommandContext. Do not call the record-time ResourceStateTracker here;
  // the tracker is not authoritative at execute-time for headless. Instead,
  // compare/validate against the observed map and update it to the barrier
  // 'after' state.
  auto& observed = ctx.observed_states;
  for (auto& b : barriers_) {
    if (b.IsMemoryBarrier()) {
      DLOG_F(4, "Headless: memory barrier on {}", b.GetResource());
      continue;
    }

    const auto resource = b.GetResource();
    const auto before = b.GetStateBefore();
    const auto after = b.GetStateAfter();

    auto it = observed.find(resource);
    if (it == observed.end()) {
      // Initialize observed state from the barrier's before so the runtime
      // view matches the recorded expectation.
      observed[resource] = before;
      DLOG_F(4, "Headless: initializing observed state for {} -> {}", resource,
        before);
    } else {
      if (it->second != before) {
        LOG_F(WARNING,
          "Headless barrier mismatch for {}: expected before={} but barrier "
          "requests {}",
          resource, it->second, before);
        DCHECK_F(
          it->second == before, "Resource state mismatch for {}", resource);
      }
    }

    // Apply the transition.
    observed[resource] = after;
  }

  DLOG_F(4, "Applied {} barriers", barriers_.size());
}

auto ResourceBarrierCommand::Serialize(std::ostream& /*os*/) const -> void { }
