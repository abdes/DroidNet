//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

// Minimal exported symbol to give the scaffolded shared library a concrete
// translation unit during Phase 0 build integration.
OXGN_VRTX_API void ModuleAnchor() { }

} // namespace oxygen::vortex
