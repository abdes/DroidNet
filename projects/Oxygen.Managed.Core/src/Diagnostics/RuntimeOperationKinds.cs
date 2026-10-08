// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable runtime operation-kind names used by editor operation results.
/// </summary>
public static class RuntimeOperationKinds
{
    /// <summary>
    /// Embedded runtime loop lifetime.
    /// </summary>
    public const string Loop = "Runtime.Loop";

    /// <summary>
    /// Embedded runtime startup.
    /// </summary>
    public const string Start = "Runtime.Start";

    /// <summary>
    /// Runtime settings write.
    /// </summary>
    public const string SettingsApply = "Runtime.Settings.Apply";

    /// <summary>
    /// Runtime viewport surface attach.
    /// </summary>
    public const string SurfaceAttach = "Runtime.Surface.Attach";

    /// <summary>
    /// Runtime viewport surface resize.
    /// </summary>
    public const string SurfaceResize = "Runtime.Surface.Resize";

    /// <summary>
    /// Runtime engine view creation.
    /// </summary>
    public const string ViewCreate = "Runtime.View.Create";

    /// <summary>
    /// Runtime engine view destruction.
    /// </summary>
    public const string ViewDestroy = "Runtime.View.Destroy";

    /// <summary>
    /// Runtime engine view camera preset change.
    /// </summary>
    public const string ViewSetCameraPreset = "Runtime.View.SetCameraPreset";

    /// <summary>
    /// Runtime engine view editor camera control-mode change.
    /// </summary>
    public const string ViewSetCameraControlMode = "Runtime.View.SetCameraControlMode";

    /// <summary>
    /// Runtime engine view switch between the editor camera and an authored scene camera.
    /// </summary>
    public const string ViewSetSceneCamera = "Runtime.View.SetSceneCamera";

    /// <summary>
    /// Runtime engine view start or stop of piloting its scene camera.
    /// </summary>
    public const string ViewSetScenePilot = "Runtime.View.SetScenePilot";

    /// <summary>
    /// Runtime engine view read of its editor camera pose for a scene node.
    /// </summary>
    public const string ViewGetCameraPose = "Runtime.View.GetCameraPose";

    /// <summary>
    /// Runtime engine view editor camera movement speed change.
    /// </summary>
    public const string ViewSetCameraMovementSpeed = "Runtime.View.SetCameraMovementSpeed";

    /// <summary>
    /// Viewport view mode, ground grid and selection outline update.
    /// </summary>
    public const string ViewSetRenderOptions = "Runtime.View.SetRenderOptions";

    /// <summary>
    /// Viewport pick of the scene nodes under a click or marquee.
    /// </summary>
    public const string ViewPick = "Runtime.View.Pick";

    /// <summary>
    /// Viewport framing of the selection or the whole scene.
    /// </summary>
    public const string ViewFrame = "Runtime.View.Frame";

    /// <summary>
    /// Selection outline update for the editing views.
    /// </summary>
    public const string SelectionOutline = "Runtime.View.SetSelectionOutline";

    /// <summary>
    /// Runtime engine view camera lens and clipping settings change.
    /// </summary>
    public const string ViewSetCameraSettings = "Runtime.View.SetCameraSettings";

    /// <summary>
    /// Runtime cooked-root refresh.
    /// </summary>
    public const string CookedRootRefresh = "Runtime.CookedRoot.Refresh";
}
