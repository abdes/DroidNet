//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

using namespace oxygen::graphics;

namespace oxygen::engine::upload {

auto DefaultUploadPolicy() -> UploadPolicy
{
  return UploadPolicy { SingleQueueStrategy().KeyFor(QueueRole::kTransfer) };
}

} // namespace oxygen::engine::upload
