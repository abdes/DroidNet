//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/Traversal.h>

auto oxygen::scene::to_string(const SceneNodeFlags& value) noexcept -> const
  char*
{
  switch (value) {
  case SceneNodeFlags::kVisible:
    return "Visible";
  case SceneNodeFlags::kStatic:
    return "Static";
  case SceneNodeFlags::kCastsShadows:
    return "CastsShadows";
  case SceneNodeFlags::kReceivesShadows:
    return "ReceivesShadows";
  case SceneNodeFlags::kRayCastingSelectable:
    return "RayCastingSelectable";
  case SceneNodeFlags::kIgnoreParentTransform:
    return "IgnoreParentTransform";

  case SceneNodeFlags::kCount:
    break;
  }
  return "__NotSupported__";
}

auto oxygen::scene::to_string(const FilterResult value) -> const char*
{
  switch (value) {
  case FilterResult::kAccept:
    return "Accept";
  case FilterResult::kReject:
    return "Reject";
  case FilterResult::kRejectSubTree:
    return "Reject SubTree";
  }

  return "__NotSupported__";
}

auto oxygen::scene::to_string(const VisitResult value) -> const char*
{
  switch (value) {
  case VisitResult::kContinue:
    return "Continue";
  case VisitResult::kSkipSubtree:
    return "Skip SubTree";
  case VisitResult::kStop:
    return "Stop";
  }

  return "__NotSupported__";
}

auto oxygen::scene::to_string(const TraversalOrder value) -> const char*
{
  switch (value) {
  case TraversalOrder::kBreadthFirst:
    return "Breadth First";
  case TraversalOrder::kPreOrder:
    return "Pre Order";
  case TraversalOrder::kPostOrder:
    return "Post Order";
  }

  return "__NotSupported__";
}
