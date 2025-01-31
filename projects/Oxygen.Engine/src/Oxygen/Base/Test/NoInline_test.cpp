//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/NoInline.h"
#include <catch2/catch_test_macros.hpp>

namespace {

OXYGEN_NOINLINE auto NoInlineFunction(const int x) -> int
{
    return x * 2;
}

TEST_CASE("NoInline compiles", "[NoInline]")
{
    REQUIRE(NoInlineFunction(2) == 4);
    REQUIRE(NoInlineFunction(3) == 6);
}

} // namespace
