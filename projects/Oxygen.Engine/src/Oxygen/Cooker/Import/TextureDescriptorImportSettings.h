//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Cooker/Import/TextureImportSettings.h>

namespace oxygen::content::import {

//! Ingress settings for schema-based texture descriptor imports.
struct TextureDescriptorImportSettings final {
  //! Path to the JSON descriptor document.
  std::string descriptor_path;

  //! Base texture settings supplied by tooling defaults/job overrides.
  /*!
   Descriptor fields are applied on top of this base and then normalized into
   the canonical texture request path.
  */
  TextureImportSettings texture = {};
};

} // namespace oxygen::content::import
