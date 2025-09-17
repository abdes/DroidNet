//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

using namespace oxygen::graphics;

namespace oxygen::engine::upload {

auto DefaultUploadPolicy() -> UploadPolicy
{
  // Provide a sensible default upload queue key.
  return UploadPolicy { SingleQueueStrategy().KeyFor(QueueRole::kTransfer) };
}

} // namespace oxygen::engine::upload
