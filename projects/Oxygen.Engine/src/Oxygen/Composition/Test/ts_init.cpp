//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Composition/TypeSystem.h"

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT_SYMBOL __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define EXPORT_SYMBOL __attribute__((visibility("default")))
#else
#  define EXPORT_SYMBOL
#endif

extern "C" bool initialize_called { false };

namespace {

extern "C" {
EXPORT_SYMBOL auto InitializeTypeRegistry() -> oxygen::TypeRegistry*
{
    // Single instance of the type registry provided by the main executable module.
    static oxygen::TypeRegistry registry {};

    initialize_called = true;
    return &registry;
}
} // extern "C"

} // namespace
