//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Engine/InputHandler.h>

#include <Oxygen/Testing/GTest.h>

class MyInputHandler final : public oxygen::engine::InputHandler {
public:
    void ProcessInput(const oxygen::platform::InputEvent& /*event*/) override { }
};

NOLINT_TEST(InputHandlerInterfaceTest, CompilesAndLinks)
{
    const MyInputHandler handler {};
    (void)handler;
}
