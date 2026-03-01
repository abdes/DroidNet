//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared Input/PAK contract catalog.
//
// This file is consumed by both C++ runtime modules and PAK/tooling code.
// Only persistable IDs/constants that must stay binary-stable belong here.
//
// Expected consumer macros (define only what you need before including):
//
//   OXNPUT_PAK_ASSET_TYPE(name, value)
//   OXNPUT_PAK_COMPONENT_TYPE(name, value)
//   OXNPUT_PAK_VERSION(name, value)
//   OXNPUT_PAK_ACTION_FLAG(name, value)
//   OXNPUT_PAK_CONTEXT_FLAG(name, value)
//   OXNPUT_PAK_MAPPING_FLAG(name, value)
//
// All macros are optional; undefined ones expand to nothing.

#include <Oxygen/Core/Meta/Input/ActionState.inc>
#include <Oxygen/Core/Meta/Input/ActionTriggers.inc>
#include <Oxygen/Core/Meta/Input/ActionValue.inc>

#ifndef OXNPUT_PAK_ASSET_TYPE
#  define OXNPUT_PAK_ASSET_TYPE(name, value)
#endif
#ifndef OXNPUT_PAK_COMPONENT_TYPE
#  define OXNPUT_PAK_COMPONENT_TYPE(name, value)
#endif
#ifndef OXNPUT_PAK_VERSION
#  define OXNPUT_PAK_VERSION(name, value)
#endif
#ifndef OXNPUT_PAK_ACTION_FLAG
#  define OXNPUT_PAK_ACTION_FLAG(name, value)
#endif
#ifndef OXNPUT_PAK_CONTEXT_FLAG
#  define OXNPUT_PAK_CONTEXT_FLAG(name, value)
#endif
#ifndef OXNPUT_PAK_MAPPING_FLAG
#  define OXNPUT_PAK_MAPPING_FLAG(name, value)
#endif

OXNPUT_PAK_ASSET_TYPE(kInputAction, 5)
OXNPUT_PAK_ASSET_TYPE(kInputMappingContext, 6)

OXNPUT_PAK_VERSION(kInputActionAssetVersion, 1)
OXNPUT_PAK_VERSION(kInputMappingContextAssetVersion, 1)

OXNPUT_PAK_ACTION_FLAG(kNone, 0)
OXNPUT_PAK_ACTION_FLAG(kConsumesInput, 0)

OXNPUT_PAK_CONTEXT_FLAG(kNone, 0)

OXNPUT_PAK_MAPPING_FLAG(kNone, 0)

#undef OXNPUT_PAK_ASSET_TYPE
#undef OXNPUT_PAK_COMPONENT_TYPE
#undef OXNPUT_PAK_VERSION
#undef OXNPUT_PAK_ACTION_FLAG
#undef OXNPUT_PAK_CONTEXT_FLAG
#undef OXNPUT_PAK_MAPPING_FLAG
