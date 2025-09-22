//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/EditorInterface/api_export.h>

OXGN_EI_API auto CreateScene(const char* name) -> bool;
OXGN_EI_API auto RemoveScene(const char* name) -> bool;
