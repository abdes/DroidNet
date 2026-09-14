//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

namespace oxygen::content::import {

//! Ingress settings for schema-based scene descriptor imports.
struct SceneDescriptorImportSettings final {
  //! Path to the JSON descriptor document.
  std::string descriptor_path;

  //! Optional cooked root destination.
  std::string cooked_root;

  //! Ordered resolver-only cooked roots; later roots have higher priority.
  std::vector<std::string> cooked_context_roots;

  //! Optional explicit job name override.
  std::string job_name;

  //! Requested content-hashing policy.
  bool with_content_hashing = true;
};

} // namespace oxygen::content::import
