//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::content::import {

//! Unique identifier for an import job.
// clang-format off
using ImportJobId = NamedType<uint64_t,
  struct ImportJobIdTag,
  Comparable,
  Hashable,
  Printable>;
// clang-format on

//! Invalid job ID constant.
inline constexpr ImportJobId kInvalidJobId { uint64_t { 0 } };

//! Convert an import job ID to a string.
inline auto to_string(ImportJobId job_id) -> std::string
{
  return std::to_string(job_id.get());
}

} // namespace oxygen::content::import
