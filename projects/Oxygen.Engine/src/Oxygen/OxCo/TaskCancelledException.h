//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>

namespace oxygen::co {

class TaskCancelledException final : public std::logic_error {
public:
    TaskCancelledException()
        : std::logic_error("invalid co_await on cancelled task")
    {
    }
    using logic_error::logic_error;
};

} // namespace oxygen::co
