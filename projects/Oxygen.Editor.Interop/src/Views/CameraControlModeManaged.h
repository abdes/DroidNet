//===----------------------------------------------------------------------===//
// Managed enum for editor viewport camera control modes.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

namespace Oxygen::Interop {

  /// <summary>
  /// Editor viewport camera navigation modes. These control the editor camera,
  /// not authored scene camera components.
  /// </summary>
  public enum class CameraControlModeManaged : System::Int32 {
    /// <summary>World-up orbit around the viewport focus point.</summary>
    OrbitTurntable = 0,

    /// <summary>Free trackball orbit around the viewport focus point.</summary>
    OrbitTrackball = 1,

    /// <summary>Right-mouse free-fly camera movement.</summary>
    Fly = 2,
  };

} // namespace Oxygen::Interop
