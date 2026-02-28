//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared script compile-mode catalog.
//
// Expected consumer macro:
//   OXGN_SCRIPT_COMPILE_MODE(name, value)
//
// Macro is optional; undefined expands to nothing.

#ifndef OXGN_SCRIPT_COMPILE_MODE
#  define OXGN_SCRIPT_COMPILE_MODE(name, value)
#endif

OXGN_SCRIPT_COMPILE_MODE(kDebug, 0)
OXGN_SCRIPT_COMPILE_MODE(kOptimized, 1)

#undef OXGN_SCRIPT_COMPILE_MODE
