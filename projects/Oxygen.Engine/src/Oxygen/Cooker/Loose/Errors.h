//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>

namespace oxygen::content::lc {

class Error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace oxygen::content::lc
