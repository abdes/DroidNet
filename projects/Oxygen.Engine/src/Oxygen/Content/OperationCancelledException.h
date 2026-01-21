//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>

namespace oxygen::content {

//! Exception thrown when a content operation is canceled.
/*!
 Thrown by coroutine-based Content load APIs when an in-flight operation is
 canceled (e.g. due to shutdown).

 This type exists to provide a stable, domain-specific cancellation signal to
 Content callers without exposing OxCo implementation details.

 @see oxygen::co::TaskCancelledException
*/
class OperationCancelledException final : public std::logic_error {
public:
  OperationCancelledException()
    : std::logic_error("content operation canceled")
  {
  }
  using logic_error::logic_error;
};

} // namespace oxygen::content
