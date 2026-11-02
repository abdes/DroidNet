//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Content/SourceKeyConflictError.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content {

SourceKeyConflictError::SourceKeyConflictError(const data::SourceKey source_key,
  std::string mount, std::string existing_mount)
  : std::runtime_error("SourceKey " + data::to_string(source_key) + " of '"
      + mount + "' already belongs to the mounted '" + existing_mount + "'")
  , source_key_(source_key)
  , mount_(std::move(mount))
  , existing_mount_(std::move(existing_mount))
{
}

} // namespace oxygen::content
