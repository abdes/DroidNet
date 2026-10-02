//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! Ingress settings for schema-based texture descriptor imports.
struct TextureDescriptorImportSettings final {
  //! Path to the JSON descriptor document.
  std::string descriptor_path;

  //! Base texture settings supplied by tooling defaults/job overrides.
  /*!
   Descriptor fields are applied on top of this base and then normalized into
   the canonical texture request path. An explicit base virtual_path supplies
   the asset identity; a conflicting descriptor identity is rejected.
  */
  TextureImportSettings texture = {};

  //! Applies the native descriptor schema and recipe to already-read source
  //! bytes. Does not require or create a cooked destination.
  OXGN_COOK_NDAPI auto Prepare(
    std::string_view bytes, std::vector<ImportDiagnostic>& diagnostics) const
    -> std::optional<TextureImportSettings>;
};

} // namespace oxygen::content::import
