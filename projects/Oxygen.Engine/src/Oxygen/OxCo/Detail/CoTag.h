//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::co::detail {

//! Base class to derive Co<T> from, to figure out if something is a `Co`.
struct CoTag { };

} // namespace oxygen::co::detail
