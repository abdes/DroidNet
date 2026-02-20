//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::internal {

class IMutationCollector;

OXGN_SCN_NDAPI auto CreateMutationCollector()
  -> std::unique_ptr<IMutationCollector>;

} // namespace oxygen::scene::internal
