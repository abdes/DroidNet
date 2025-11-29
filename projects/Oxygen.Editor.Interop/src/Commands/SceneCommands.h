//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// This file used to contain all Scene command classes in a single header.
// It has been split so each class now lives in its own header. For
// compatibility, this file aggregates the new headers so existing includes
// continue to work.

#include "CreateSceneNodeCommand.h"
#include "RemoveSceneNodeCommand.h"
#include "RenameSceneNodeCommand.h"
#include "SetLocalTransformCommand.h"
#include "CreateBasicMeshCommand.h"
#include "SetVisibilityCommand.h"
#include "ReparentSceneNodeCommand.h"
#include "UpdateTransformsForNodesCommand.h"
