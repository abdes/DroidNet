//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/*!
 @file cs_init.cpp

 This file contains the implementation of the `InitializeTypeRegistry` and
 `InitializeComponentPoolRegistry`, two critical parts of the Oxygen Composition
 system. It is included in the composition system library and in the dedicated
 `oxygen::cs-init` shared library for the most common and natural usage
 scenarios:
  - Composition system library as a DLL, linked with the main executable.
  - Composition system library as a static library, and main executable linked
    with the `oxygen::cs-init` DLL.

 **Note: All-static builds**

 Sometimes, however, the entire build must be static only, and it is not
 possible for the main executable to link with the `oxygen::cs-init` or load it
 dynamically. In such case, this file must be linked with the main executable to
 ensure that the single instances of the `TypeRegistry` and the
 `ComponentPoolRegistry` are truly global single instances. Additionally, when
 using `clang` or `gcc`, we must explicitly tell them to export symbols even
 from the executable. This is needed for the composition system to find the
 `InitializeTypeRegistry` function defined in the executable.

 ```cmake
 target_link_options(
   ${MY_TARGET}
   PRIVATE
     $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wl,--export-dynamic>
 )
 ```
*/

#include <Oxygen/Composition/ComponentPoolRegistry.h>
#include <Oxygen/Composition/TypeSystem.h>

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT_SYMBOL __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define EXPORT_SYMBOL __attribute__((visibility("default")))
#else
#  define EXPORT_SYMBOL
#endif

extern "C" {
EXPORT_SYMBOL auto InitializeTypeRegistry() -> oxygen::TypeRegistry*
{
  // Single instance of the type registry provided by the main executable
  // module.
  static oxygen::TypeRegistry registry {};
  return &registry;
}

EXPORT_SYMBOL auto InitializeComponentPoolRegistry()
  -> oxygen::ComponentPoolRegistry*
{
  // Single instance of the component pool registry provided by the main
  // executable module.
  static oxygen::ComponentPoolRegistry registry {};
  return &registry;
}

} // extern "C"
