//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Unreachable.h>

#include <catch2/catch_test_macros.hpp>

#include <Oxygen/Base/Compilers.h>

namespace {

OXYGEN_DIAGNOSTIC_PUSH
#if defined(OXYGEN_GNUC_VERSION) || defined(OXYGEN_CLANG_VERSION)
OXYGEN_DIAGNOSTIC_DISABLE(-Wunreachable - code) // For Clang and GCC
#elif defined(OXYGEN_MSVC_VERSION)
OXYGEN_DIAGNOSTIC_DISABLE(4702) // For MSVC
#endif
TEST_CASE("Unreachable code", "[Unreachable]")
{
    SECTION("is never reached")
    {
        // ReSharper disable All
        bool unreachable = false;
        if (unreachable) {
            oxygen::Unreachable();
            FAIL_CHECK("Unreachable code was reached");
        }
    }
}
OXYGEN_DIAGNOSTIC_POP

} // namespace
