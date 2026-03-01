//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace oxygen::content::import {

//! Tooling-facing settings for one input import job.
/*!
 This DTO is intentionally minimal and orchestration-only. Asset semantics are
 authored in source JSON and parsed by `InputImportPipeline`.
*/
struct InputImportSettings final {
  //! Source input JSON file path (`*.input.json` or `*.input-action.json`).
  std::string source_path;
};

} // namespace oxygen::content::import
