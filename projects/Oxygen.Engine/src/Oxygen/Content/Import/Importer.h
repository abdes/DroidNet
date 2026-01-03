//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Content/Import/CookedContentWriter.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/ImportRequest.h>

namespace oxygen::content::import {

//! Minimal interface implemented by authoring-format import backends.
/*!
 This interface is intentionally tiny.

 Responsibilities of a backend:
 - parse the source format (FBX, glTF/GLB, ...)
 - apply import options and coordinate conversion
 - generate runtime-compatible cooked descriptors/resources
 - emit them through CookedContentWriter

 Responsibilities NOT in a backend:
 - defining cooked container layout/index formats
 - writing container metadata/index files
 - exposing third-party parser headers in public Oxygen APIs
*/
class Importer {
public:
  virtual ~Importer() = default;

  //! A stable identifier used for logging and diagnostics.
  [[nodiscard]] virtual auto Name() const noexcept -> std::string_view = 0;

  //! Returns true if this backend supports `format`.
  [[nodiscard]] virtual auto Supports(ImportFormat format) const noexcept
    -> bool
    = 0;

  //! Import the request and emit cooked output.
  /*!
   @throw std::runtime_error for hard failures (parse errors, invalid inputs).
  */
  virtual auto Import(const ImportRequest& request, CookedContentWriter& out)
    -> void
    = 0;
};

} // namespace oxygen::content::import
