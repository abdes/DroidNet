//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string>

#include <Oxygen/Config/PathFinderConfig.h>

namespace oxygen {

struct RendererConfig {
  //! Immutable configuration for path resolution.
  PathFinderConfig path_finder_config {};

  //! Upload queue key to use for staging/upload recording. Renderer will set
  //! this into the UploadPolicy passed to the UploadCoordinator. This field
  //! is required; do not default-initialize.
  std::string upload_queue_key;

  //! Maximum number of simultaneously prepared views the renderer keeps alive
  //! before evicting the least-recently-used entry. Default keeps legacy 8.
  std::size_t max_active_views { 8 };
};

} // namespace oxygen
