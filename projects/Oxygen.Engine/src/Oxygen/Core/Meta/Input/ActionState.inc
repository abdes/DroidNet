//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared ActionState bitmask catalog.
//
// Expected consumer macro:
//   OXNPUT_ACTION_STATE(name, value)
//
// Macro is optional; undefined expands to nothing.

#ifndef OXNPUT_ACTION_STATE
#  define OXNPUT_ACTION_STATE(name, value)
#endif

OXNPUT_ACTION_STATE(kNone, 0)
OXNPUT_ACTION_STATE(kOngoing, 1)
OXNPUT_ACTION_STATE(kCanceled, 2)
OXNPUT_ACTION_STATE(kCompleted, 3)
OXNPUT_ACTION_STATE(kTriggered, 4)

#undef OXNPUT_ACTION_STATE
