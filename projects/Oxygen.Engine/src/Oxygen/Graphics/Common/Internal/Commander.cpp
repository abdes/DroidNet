//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>

namespace oxygen::graphics::internal {

auto Commander::PrepareCommandRecorder(
  std::unique_ptr<CommandRecorder> recorder, const SubmissionPolicy policy,
  std::shared_ptr<Graphics> backend_owner,
  std::shared_ptr<BackendLifetime> lifetime) -> CommandRecording
{
  return { std::move(recorder), reclaimer_, policy, std::move(backend_owner),
    std::move(lifetime) };
}

} // namespace oxygen::graphics::internal
