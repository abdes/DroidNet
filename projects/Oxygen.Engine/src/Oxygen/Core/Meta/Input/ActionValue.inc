//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared ActionValueType ID catalog.
//
// Expected consumer macro:
//   OXNPUT_ACTION_VALUE_TYPE(name, value)
//
// Macro is optional; undefined expands to nothing.

#ifndef OXNPUT_ACTION_VALUE_TYPE
#  define OXNPUT_ACTION_VALUE_TYPE(name, value)
#endif

OXNPUT_ACTION_VALUE_TYPE(kBool, 0)
OXNPUT_ACTION_VALUE_TYPE(kAxis1D, 1)
OXNPUT_ACTION_VALUE_TYPE(kAxis2D, 2)

#undef OXNPUT_ACTION_VALUE_TYPE
