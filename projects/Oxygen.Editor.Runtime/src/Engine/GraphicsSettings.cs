// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Graphics backend settings.
/// </summary>
public sealed class GraphicsSettings
{
    /// <summary>
    ///     Gets or sets a value indicating whether the native graphics debug layer is enabled.
    /// </summary>
    public bool? EnableDebugLayer { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether native graphics validation is enabled.
    /// </summary>
    public bool? EnableValidation { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether Nsight Aftermath is enabled.
    /// </summary>
    public bool? EnableAftermath { get; set; }

    /// <summary>
    ///     Gets or sets preferred GPU card name.
    /// </summary>
    public string? PreferredCardName { get; set; }

    /// <summary>
    ///     Gets or sets preferred GPU device ID.
    /// </summary>
    public long? PreferredCardDeviceId { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether graphics run without a native window.
    /// </summary>
    public bool? Headless { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether graphics ImGui integration is enabled.
    /// </summary>
    public bool? EnableImGui { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether presentation synchronizes to vertical refresh.
    /// </summary>
    public bool? EnableVSync { get; set; }

    /// <summary>
    ///     Gets or sets frame capture settings.
    /// </summary>
    public FrameCaptureSettings FrameCapture { get; set; } = new();

    /// <summary>
    ///     Gets or sets backend-specific configuration JSON.
    /// </summary>
    public string? Extra { get; set; }
}
