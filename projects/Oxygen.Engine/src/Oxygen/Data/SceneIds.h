//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Data/StableId.h>

namespace oxygen::data {

struct NodeIdTag {
  static constexpr std::string_view kName = "Node";
};

//! The identity of a scene node, unique within the scene or model that
//! defines it.
/*!
 An authored node's ID is the editor's node ID; a model node's is derived from
 its anchor and the model's source identity. Changes and node paths name nodes
 by it alone.
*/
using NodeId = StableId<NodeIdTag>;

struct ScriptSlotIdTag {
  static constexpr std::string_view kName = "Script slot";
};

//! The identity of a script slot, unique among its node's slots; the editor
//! gives each slot one when it is created.
using ScriptSlotId = StableId<ScriptSlotIdTag>;

} // namespace oxygen::data
