//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>

namespace oxygen::engine::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::engine::internal

namespace oxygen::vortex::internal {

auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}

} // namespace oxygen::vortex::internal
