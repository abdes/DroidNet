//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <EditorModule/EditorModule.h>
#include <EditorModule/EditorView.h>

namespace Oxygen::Interop {

  /// <summary>A scene node with visible pixels in a picked rectangle.</summary>
  public
  ref class ViewPickHitManaged sealed {
  public:
    property System::Guid NodeId;

    /// <summary>Device depth of the node's nearest pixel.</summary>
    property float Depth;

    /// <summary>Geometry slot under the pixel closest to the centre.</summary>
    property System::UInt32 GeometrySlot;

    /// <summary>Pixels from the rectangle centre to the node's closest
    /// pixel.</summary>
    property float CenterDistance;
  };

  /// <summary>
  /// Nodes found by a viewport pick, closest to the rectangle centre first.
  /// </summary>
  public
  ref class ViewPickResultManaged sealed {
  public:
    property array<ViewPickHitManaged^>^ Hits;

    /// <summary>World position under the first hit, if any.</summary>
    property System::Nullable<System::Numerics::Vector3> WorldPosition;

    static ViewPickResultManaged^ FromNative(
      const ::oxygen::interop::module::EditorPickResult& result) {
      auto managed = gcnew ViewPickResultManaged();
      managed->Hits = gcnew array<ViewPickHitManaged^>(
        static_cast<int>(result.hits.size()));
      for (int i = 0; i < managed->Hits->Length; ++i) {
        const auto& hit = result.hits[static_cast<std::size_t>(i)];
        auto bytes = gcnew array<System::Byte>(16);
        for (int b = 0; b < 16; ++b) {
          bytes[b] = hit.node[static_cast<std::size_t>(b)];
        }
        auto item = gcnew ViewPickHitManaged();
        item->NodeId = System::Guid(bytes);
        item->Depth = hit.depth;
        item->GeometrySlot = hit.geometry_slot;
        item->CenterDistance = hit.center_distance;
        managed->Hits[i] = item;
      }
      if (result.world_position.has_value()) {
        managed->WorldPosition = System::Numerics::Vector3(
          result.world_position->x, result.world_position->y,
          result.world_position->z);
      }
      return managed;
    }
  };

  /// <summary>What a frame request did to a view's editor camera.</summary>
  public enum class ViewFramingOutcomeManaged : System::Int32 {
    /// <summary>The editor camera is moving to frame the bounds.</summary>
    Framed = 0,

    /// <summary>None of the requested nodes exist.</summary>
    NothingToFrame = 1,

    /// <summary>The view looks through a scene camera, which framing never
    /// moves.</summary>
    ViewingSceneCamera = 2,

    /// <summary>The view does not exist.</summary>
    NoView = 3,

    /// <summary>The bounds are not finite; the view is unchanged.</summary>
    InvalidBounds = 4,
  };

} // namespace Oxygen::Interop
