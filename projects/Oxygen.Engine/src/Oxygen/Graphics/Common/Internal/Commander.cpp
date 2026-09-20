//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Internal/Commander.h>

#include <utility>

#include <Oxygen/Graphics/Common/CommandRecorder.h>

namespace oxygen::graphics::internal {

auto Commander::PrepareCommandRecorder(
  std::unique_ptr<CommandRecorder> recorder, const SubmissionPolicy policy)
  -> CommandRecording
{
  return { std::move(recorder), reclaimer_, policy };
}

} // namespace oxygen::graphics::internal
