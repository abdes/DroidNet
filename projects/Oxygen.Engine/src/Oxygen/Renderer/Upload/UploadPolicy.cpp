//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

namespace oxygen::engine::upload {

auto DefaultUploadPolicy() -> UploadPolicy
{
  using oxygen::graphics::QueueRole;
  using oxygen::graphics::SingleQueueStrategy;
  return UploadPolicy { SingleQueueStrategy().KeyFor(QueueRole::kTransfer) };
}

UploadPolicy::UploadPolicy(oxygen::graphics::QueueKey qkey) noexcept
  : UploadPolicy(std::move(qkey), AlignmentPolicy {})
{
}

UploadPolicy::UploadPolicy(
  oxygen::graphics::QueueKey qkey, AlignmentPolicy alignment_policy) noexcept
  : alignment(alignment_policy)
  , upload_queue_key(std::move(qkey))
{
}

} // namespace oxygen::engine::upload
