//===----------------------------------------------------------------------===//
// Managed enum for viewport camera view presets.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <EditorModule/EditorView.h>

namespace Oxygen::Interop {

  /// <summary>
  /// Viewport camera view presets used by editor viewports.
  /// </summary>
  public enum class CameraViewPresetManaged : System::Int32 {
    /// <summary>Perspective view (free camera).</summary>
    Perspective = 0,

    /// <summary>Top orthographic view.</summary>
    Top = 1,

    /// <summary>Bottom orthographic view.</summary>
    Bottom = 2,

    /// <summary>Left orthographic view.</summary>
    Left = 3,

    /// <summary>Right orthographic view.</summary>
    Right = 4,

    /// <summary>Front orthographic view.</summary>
    Front = 5,

    /// <summary>Back orthographic view.</summary>
    Back = 6,
  };

  [[nodiscard]] inline auto ToNativeCameraViewPreset(
    CameraViewPresetManaged preset)
    -> ::oxygen::interop::module::CameraViewPreset {
    using NativePreset = ::oxygen::interop::module::CameraViewPreset;
    switch (preset) {
    case CameraViewPresetManaged::Top:
      return NativePreset::kTop;
    case CameraViewPresetManaged::Bottom:
      return NativePreset::kBottom;
    case CameraViewPresetManaged::Left:
      return NativePreset::kLeft;
    case CameraViewPresetManaged::Right:
      return NativePreset::kRight;
    case CameraViewPresetManaged::Front:
      return NativePreset::kFront;
    case CameraViewPresetManaged::Back:
      return NativePreset::kBack;
    case CameraViewPresetManaged::Perspective:
    default:
      return NativePreset::kPerspective;
    }
  }

} // namespace Oxygen::Interop
