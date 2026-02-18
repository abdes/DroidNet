//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared ComponentType ID catalog.
//
// Expected consumer macro:
//   OXDAT_COMPONENT_TYPE(name, value)
//
// Macro is optional; undefined expands to nothing.

#ifndef OXDAT_COMPONENT_TYPE
#  define OXDAT_COMPONENT_TYPE(name, value)
#endif

OXDAT_COMPONENT_TYPE(kUnknown, 0)

OXDAT_COMPONENT_TYPE(kRenderable, 0x4853454D)         // 'MESH'
OXDAT_COMPONENT_TYPE(kPerspectiveCamera, 0x4D414350)  // 'PCAM'
OXDAT_COMPONENT_TYPE(kOrthographicCamera, 0x4D41434F) // 'OCAM'

OXDAT_COMPONENT_TYPE(kDirectionalLight, 0x54494C44) // 'DLIT'
OXDAT_COMPONENT_TYPE(kPointLight, 0x54494C50)       // 'PLIT'
OXDAT_COMPONENT_TYPE(kSpotLight, 0x54494C53)        // 'SLIT'
OXDAT_COMPONENT_TYPE(kScripting, 0x50524353)        // 'SCRP'

#undef OXDAT_COMPONENT_TYPE
