// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable runtime operation-kind names used by editor operation results.
/// </summary>
public static class RuntimeOperationKinds
{
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
    /// Runtime cooked-root refresh.
    /// </summary>
    public const string CookedRootRefresh = "Runtime.CookedRoot.Refresh";
}
