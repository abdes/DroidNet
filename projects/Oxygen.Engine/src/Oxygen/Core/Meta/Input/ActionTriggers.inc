//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared ActionTriggers ID catalog.
//
// Expected consumer macros:
//   OXNPUT_ACTION_TRIGGER_TYPE(name, value)
//   OXNPUT_ACTION_TRIGGER_BEHAVIOR(name, value)
//
// Macros are optional; undefined expands to nothing.

#ifndef OXNPUT_ACTION_TRIGGER_TYPE
#  define OXNPUT_ACTION_TRIGGER_TYPE(name, value)
#endif
#ifndef OXNPUT_ACTION_TRIGGER_BEHAVIOR
#  define OXNPUT_ACTION_TRIGGER_BEHAVIOR(name, value)
#endif

OXNPUT_ACTION_TRIGGER_TYPE(kPressed, 0)
OXNPUT_ACTION_TRIGGER_TYPE(kReleased, 1)
OXNPUT_ACTION_TRIGGER_TYPE(kDown, 2)
OXNPUT_ACTION_TRIGGER_TYPE(kHold, 3)
OXNPUT_ACTION_TRIGGER_TYPE(kHoldAndRelease, 4)
OXNPUT_ACTION_TRIGGER_TYPE(kPulse, 5)
OXNPUT_ACTION_TRIGGER_TYPE(kTap, 6)
OXNPUT_ACTION_TRIGGER_TYPE(kChord, 7)
OXNPUT_ACTION_TRIGGER_TYPE(kActionChain, 8)
OXNPUT_ACTION_TRIGGER_TYPE(kCombo, 9)

OXNPUT_ACTION_TRIGGER_BEHAVIOR(kExplicit, 0)
OXNPUT_ACTION_TRIGGER_BEHAVIOR(kImplicit, 1)
OXNPUT_ACTION_TRIGGER_BEHAVIOR(kBlocker, 2)

#undef OXNPUT_ACTION_TRIGGER_TYPE
#undef OXNPUT_ACTION_TRIGGER_BEHAVIOR
