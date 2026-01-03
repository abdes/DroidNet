//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>

#include <Oxygen/Content/api_export.h>

#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Importer.h>

namespace oxygen::content::import {

//! Single entry point for importing authoring formats into Oxygen.
/*!
 AssetImporter is the façade used by tooling/offline pipelines to import FBX and
 glTF/GLB into Oxygen's runtime-compatible loose cooked layout.

 Implementation notes:
 - Concrete importer backends are internal implementation details.
 - No third-party parser headers are exposed by this API.

 @warning The importer emits cooked bytes that must match the runtime loaders'
   expectations (see Oxygen/Data/PakFormat.h and Content/Loaders/*).
*/
class AssetImporter {
public:
  //! Construct with built-in backends.
  /*! Implemented out-of-line to keep third-party headers private. */
  OXGN_CNTT_API AssetImporter();

  //! Construct with explicit backends (dependency injection).
  OXGN_CNTT_API explicit AssetImporter(
    std::vector<std::unique_ptr<Importer>> backends);

  OXGN_CNTT_API ~AssetImporter();

  OXYGEN_MAKE_NON_COPYABLE(AssetImporter)
  OXYGEN_DEFAULT_MOVABLE(AssetImporter)

  //! Import a source file and emit a loose cooked container to disk.
  /*!
   @param request Import request.
   @return Import report.

   @throw std::runtime_error on hard failures (IO, parse errors).
  */
  [[nodiscard]] OXGN_CNTT_NDAPI auto ImportToLooseCooked(
    const ImportRequest& request) -> ImportReport;

protected:
  //! Detect an import format; protected to allow test overrides.
  [[nodiscard]] OXGN_CNTT_NDAPI virtual auto DetectFormat(
    const std::filesystem::path& path) const -> ImportFormat;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
