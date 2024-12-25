//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Common/CommandQueue.h"

#include "Oxygen/Base/Logging.h"

using namespace oxygen::renderer;

void CommandQueue::Initialize()
{
  Release();
  OnInitialize();
  fence_ = CreateSynchronizationCounter();
  CHECK_NOTNULL_F(fence_);
  fence_->Initialize();
  ShouldRelease(true);
}
