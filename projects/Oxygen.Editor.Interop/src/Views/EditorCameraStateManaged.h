//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <EditorModule/EditorView.h>

namespace Oxygen::Interop {

  /// <summary>
  /// The editor navigation camera's state: what a pane keeps when its view is
  /// released and what a new view starts from.
  /// </summary>
  public
  ref class EditorCameraStateManaged sealed {
  public:
    property System::Numerics::Vector3 Position;
    property System::Numerics::Quaternion Rotation;

    /// <summary>The point the orbit navigation turns around.</summary>
    property System::Numerics::Vector3 FocusPoint;

    /// <summary>Half-height of the orthographic presets' view volume.</summary>
    property float OrthographicSize;

    static EditorCameraStateManaged^ FromNative(
      const ::oxygen::interop::module::EditorCameraState& state) {
      auto managed = gcnew EditorCameraStateManaged();
      managed->Position = System::Numerics::Vector3(
        state.position.x, state.position.y, state.position.z);
      managed->Rotation = System::Numerics::Quaternion(state.rotation.x,
        state.rotation.y, state.rotation.z, state.rotation.w);
      managed->FocusPoint = System::Numerics::Vector3(
        state.focus_point.x, state.focus_point.y, state.focus_point.z);
      managed->OrthographicSize = state.ortho_half_height;
      return managed;
    }

    auto ToNative() -> ::oxygen::interop::module::EditorCameraState {
      return ::oxygen::interop::module::EditorCameraState {
        .position = { Position.X, Position.Y, Position.Z },
        // glm::quat takes w first.
        .rotation = { Rotation.W, Rotation.X, Rotation.Y, Rotation.Z },
        .focus_point = { FocusPoint.X, FocusPoint.Y, FocusPoint.Z },
        .ortho_half_height = OrthographicSize,
      };
    }
  };

} // namespace Oxygen::Interop
