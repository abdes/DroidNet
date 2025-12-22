//===----------------------------------------------------------------------===//
// Managed enum for viewport camera view presets.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

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

} // namespace Oxygen::Interop
