//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <EditorModule/TransformGizmoController.h>
#include <Views/ViewIdManaged.h>

namespace Oxygen::Interop {

  /// <summary>The viewport tool that decides which gizmo the selection
  /// shows.</summary>
  public enum class TransformToolManaged : System::Int32 {
    Select = 0,
    Translate = 1,
    Rotate = 2,
    Scale = 3,
  };

  /// <summary>The axes a translate or rotate gizmo follows.</summary>
  public enum class TransformSpaceManaged : System::Int32 {
    World = 0,
    Local = 1,
  };

  /// <summary>A gizmo part the pointer can grab.</summary>
  public enum class GizmoHandleManaged : System::Int32 {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3,
    XY = 4,
    XZ = 5,
    YZ = 6,
    /// <summary>Translate in the view plane, or scale uniformly.</summary>
    Center = 7,
    /// <summary>Rotate about the view direction.</summary>
    View = 8,
  };

  /// <summary>What happened in a gizmo interaction.</summary>
  public enum class TransformGizmoEventKindManaged : System::Int32 {
    Hover = 0,
    Begin = 1,
    Update = 2,
    Commit = 3,
    Cancel = 4,
    /// <summary>An orientation triad axis was clicked.</summary>
    ViewAxis = 5,
  };

  /// <summary>A scene helper part the pointer can drag to edit a
  /// light.</summary>
  public enum class HelperHandleManaged : System::Int32 {
    None = 0,
    Range = 1,
    InnerCone = 2,
    OuterCone = 3,
  };

  /// <summary>A manipulated node's new local transform.</summary>
  public
  ref class TransformGizmoTargetManaged sealed {
  public:
    property System::Guid NodeId;
    property System::Numerics::Vector3 Position;
    property System::Numerics::Quaternion Rotation;
    property System::Numerics::Vector3 Scale;
  };

  /// <summary>A gizmo interaction the editor applies to the authored
  /// scene.</summary>
  public
  ref class TransformGizmoEventManaged sealed {
  public:
    property TransformGizmoEventKindManaged Kind;
    property ViewIdManaged ViewId;
    property TransformToolManaged Tool;
    property GizmoHandleManaged Handle;

    /// <summary>The drag began with Alt held: it moves copies.</summary>
    property bool Duplicate;

    /// <summary>For Hover: the pointer is over a handle.</summary>
    property bool Hovering;

    /// <summary>False while the drag asks for a result the nodes cannot
    /// represent.</summary>
    property bool Representable;

    /// <summary>For Update and Commit: every target's new local
    /// transform.</summary>
    property array<TransformGizmoTargetManaged^>^ Targets;

    /// <summary>Bits 0-2: the X, Y and Z readout values apply.</summary>
    property System::Int32 ReadoutAxes;

    /// <summary>Metres per axis, degrees (in X) or scale factors.</summary>
    property System::Numerics::Vector3 ReadoutValues;

    /// <summary>The pivot in the view's pixels, when in front of the
    /// camera.</summary>
    property System::Nullable<System::Numerics::Vector2> PivotPixel;

    /// <summary>The pointer in the view's pixels.</summary>
    property System::Numerics::Vector2 PointerPixel;

    /// <summary>The event comes from a scene helper or the orientation
    /// triad.</summary>
    property bool Helper;

    /// <summary>For helper drags: the light being edited.</summary>
    property System::Guid NodeId;

    /// <summary>For helper drags: the dragged handle.</summary>
    property HelperHandleManaged HelperHandle;

    /// <summary>For helper drags: metres for a range, radians for a cone
    /// angle.</summary>
    property float Value;

    static System::Guid ToGuid(
      const ::oxygen::interop::module::UuidKey& id) {
      auto bytes = gcnew array<System::Byte>(16);
      for (int b = 0; b < 16; ++b) {
        bytes[b] = id[static_cast<std::size_t>(b)];
      }
      return System::Guid(bytes);
    }

    static TransformGizmoEventManaged^ FromNative(
      const ::oxygen::interop::module::TransformGizmoEvent& event) {
      auto managed = gcnew TransformGizmoEventManaged();
      managed->Kind = static_cast<TransformGizmoEventKindManaged>(event.kind);
      managed->ViewId = ViewIdManaged::FromNative(event.view);
      managed->Tool = static_cast<TransformToolManaged>(event.tool);
      managed->Handle = static_cast<GizmoHandleManaged>(event.handle);
      managed->Duplicate = event.duplicate;
      managed->Hovering = event.hovering;
      managed->Representable = event.representable;
      managed->Targets = gcnew array<TransformGizmoTargetManaged^>(
        static_cast<int>(event.targets.size()));
      for (int i = 0; i < managed->Targets->Length; ++i) {
        const auto& target = event.targets[static_cast<std::size_t>(i)];
        auto item = gcnew TransformGizmoTargetManaged();
        item->NodeId = ToGuid(target.id);
        item->Position = System::Numerics::Vector3(
          target.position.x, target.position.y, target.position.z);
        item->Rotation = System::Numerics::Quaternion(target.rotation.x,
          target.rotation.y, target.rotation.z, target.rotation.w);
        item->Scale = System::Numerics::Vector3(
          target.scale.x, target.scale.y, target.scale.z);
        managed->Targets[i] = item;
      }
      managed->ReadoutAxes = static_cast<System::Int32>(event.readout.axes);
      managed->ReadoutValues = System::Numerics::Vector3(event.readout.values.x,
        event.readout.values.y, event.readout.values.z);
      if (event.pivot_pixel.has_value()) {
        managed->PivotPixel = System::Numerics::Vector2(
          event.pivot_pixel->x, event.pivot_pixel->y);
      }
      managed->PointerPixel = System::Numerics::Vector2(
        event.pointer_pixel.x, event.pointer_pixel.y);
      managed->Helper = event.helper;
      managed->NodeId = ToGuid(event.node);
      managed->HelperHandle
        = static_cast<HelperHandleManaged>(event.helper_handle);
      managed->Value = event.value;
      return managed;
    }
  };

} // namespace Oxygen::Interop
