//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

namespace oxygen::content::import {

//! Tooling-facing settings for one input import job.
/*!
 This DTO is intentionally minimal and orchestration-only. Asset semantics are
 authored in source JSON and parsed by `InputImportPipeline`.
*/
struct InputImportSettings final {
  //! Source input JSON file path (`*.input.json`).
  std::string source_path;
  //! The source's identity, its path without the authoring `.json`
  //! (`/Content/Input/Player.input`); its products are named under it.
  std::string virtual_path;
  //! Destination cooked root directory (absolute path when provided).
  std::string cooked_root;
  //! Published cooked roots, lowest precedence first, that hold the actions
  //! of other sources this source maps by virtual path.
  std::vector<std::string> cooked_context_roots;
};

} // namespace oxygen::content::import
