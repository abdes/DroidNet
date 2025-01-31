//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/ReturnAddress.h"
#include <catch2/catch_test_macros.hpp>

namespace {

auto GetReturnAddress() -> void*
{
    return OXYGEN_RETURN_ADDRESS();
}

TEST_CASE("ReturnAddress macro", "[ReturnAddress]")
{
    SECTION("returns non null address")
    {
        void* address = GetReturnAddress();
        REQUIRE(address != nullptr);
    }
}

} // namespace
