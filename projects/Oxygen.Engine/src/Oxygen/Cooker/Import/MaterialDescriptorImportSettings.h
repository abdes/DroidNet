//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace oxygen::content::import {

//! Ingress settings for schema-based material descriptor imports.
struct MaterialDescriptorImportSettings final {
  //! Path to the JSON descriptor document.
  std::string descriptor_path;

  //! Optional cooked root destination.
  std::string cooked_root;

  //! Optional explicit job name override.
  std::string job_name;

  //! Requested content-hashing policy.
  bool with_content_hashing = true;
};

} // namespace oxygen::content::import
