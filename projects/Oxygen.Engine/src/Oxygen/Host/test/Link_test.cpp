//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Host/SceneStage.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const oxygen::host::SceneStage stage;
  const auto extent = oxygen::host::ParseWindowResolution("640x480");
  return stage.TryGetScene() == nullptr && extent.width == 640U ? EXIT_SUCCESS
                                                                : EXIT_FAILURE;
}
