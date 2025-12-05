//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Renderer/Test/Upload/RingBufferStagingFixture.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

// Implementation of InlineCoordinatorTagFactory. Provides access to
// InlineCoordinatorTag capability tokens, only from the engine core. When
// building tests, allow tests to override by defining OXYGEN_ENGINE_TESTING.
#if defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::upload::internal {
auto InlineCoordinatorTagFactory::Get() noexcept -> InlineCoordinatorTag
{
  return InlineCoordinatorTag {};
}
} // oxygen::engine::upload::internal
#endif
