//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/core/system.h"

#include <Oxygen/Testing/GTest.h>

class MySystem final : public oxygen::engine::System {
public:
    void Update(const oxygen::engine::SystemUpdateContext& /*context*/) override
    {
    }
};

NOLINT_TEST(SystemInterfaceTest, CompilesAndLinks)
{
    const MySystem system {};
    (void)system;
}
