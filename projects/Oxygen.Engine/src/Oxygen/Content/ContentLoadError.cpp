//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string_view>

#include <Oxygen/Content/ContentLoadError.h>

namespace oxygen::content {

auto to_string(const ContentLoadFailure value) noexcept -> std::string_view
{
  switch (value) {
  case ContentLoadFailure::kContentInvalid:
    return "content_invalid";
  case ContentLoadFailure::kDependencyMissing:
    return "dependency_missing";
  case ContentLoadFailure::kDependencyFailed:
    return "dependency_failed";
  case ContentLoadFailure::kReferenceInvalid:
    return "reference_invalid";
  case ContentLoadFailure::kBindingFailed:
    return "binding_failed";
  }
  return "unknown";
}

} // namespace oxygen::content
