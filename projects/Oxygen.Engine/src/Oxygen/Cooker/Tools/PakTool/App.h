//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <iosfwd>
#include <span>

#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>

namespace oxygen::content::pak::tool {

[[nodiscard]] auto RunPakToolApp(std::span<char*> argv, std::ostream& out,
  std::ostream& err, IRequestPreparationFileSystem& prep_fs,
  IArtifactFileSystem& artifact_fs) -> int;

} // namespace oxygen::content::pak::tool
