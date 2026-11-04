//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Why the import must not read `file`, a file a model names; nothing when it
//! may. The model's file chooses this path, so it decides what would be read.
/*!
 Any path on a network server the model is not itself on is refused from its
 text, before anything touches the file: even asking whether it exists makes
 the machine contact that server and offer it the user's credentials. A path of
 the model's own server passes, for a project kept on a share.

 With the job's mount table (`ImportRequest::source_mounts`), the file must lie
 in a `Source` mount. Without one, as a tool's direct `--output` import runs,
 the remaining paths are the user's own local files and may be read.

 The answer is a clause to follow the file's name in a message: "which lies in
 no source mount".
*/
OXGN_COOK_NDAPI auto RefusedModelDependency(const ImportRequest& request,
  const std::filesystem::path& file) -> std::optional<std::string>;

//! How a report names a file a model names: relative to the model's folder, or
//! whole when no relative path leads there, as on another drive.
OXGN_COOK_NDAPI auto ModelDependencyReportPath(
  const std::filesystem::path& file, const std::filesystem::path& model)
  -> std::string;

} // namespace oxygen::content::import::internal
