// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Graphics frame-capture settings.
/// </summary>
public sealed class FrameCaptureSettings
{
    /// <summary>
    ///     Gets or sets the frame-capture provider requested by the editor.
    /// </summary>
    public FrameCaptureProviderManaged? Provider { get; set; }

    /// <summary>
    ///     Gets or sets how the native graphics backend should initialize frame capture support.
    /// </summary>
    public FrameCaptureInitModeManaged? InitMode { get; set; }

    /// <summary>
    ///     Gets or sets the first frame to capture.
    /// </summary>
    public ulong? FromFrame { get; set; }

    /// <summary>
    ///     Gets or sets the number of frames to capture.
    /// </summary>
    public uint? FrameCount { get; set; }

    /// <summary>
    ///     Gets or sets the explicit frame-capture module path.
    /// </summary>
    public string? ModulePath { get; set; }

    /// <summary>
    ///     Gets or sets the frame-capture output file template.
    /// </summary>
    public string? CaptureFileTemplate { get; set; }
}
