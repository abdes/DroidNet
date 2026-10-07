// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Microsoft.UI;

namespace Oxygen.Editor.World.Messages;

/// <summary>A viewport camera action requested for a scene camera node.</summary>
internal enum SceneCameraCommand
{
    /// <summary>Render the active viewport through the camera.</summary>
    LookThrough,

    /// <summary>Return the active viewport to its editor camera.</summary>
    ReturnToEditorCamera,

    /// <summary>Look through the camera and move it with viewport navigation.</summary>
    Pilot,

    /// <summary>Stop moving the camera with viewport navigation.</summary>
    StopPiloting,

    /// <summary>Move the camera to the active viewport's editor camera.</summary>
    AlignToView,
}

/// <summary>
/// Asks the window's active scene editor to apply a camera action in its active viewport.
/// </summary>
/// <param name="WindowId">The editor window owning the request.</param>
/// <param name="NodeId">The camera node.</param>
/// <param name="Command">The requested action.</param>
internal sealed class SceneCameraCommandMessage(WindowId WindowId, Guid NodeId, SceneCameraCommand Command)
    : AsyncRequestMessage<bool>
{
    /// <summary>Gets the editor window owning the request.</summary>
    public WindowId WindowId { get; } = WindowId;

    /// <summary>Gets the camera node.</summary>
    public Guid NodeId { get; } = NodeId;

    /// <summary>Gets the requested action.</summary>
    public SceneCameraCommand Command { get; } = Command;
}

/// <summary>What the window's active viewport shows.</summary>
/// <param name="ViewedCameraId">The scene camera the viewport looks through, or <see langword="null"/> for its editor camera.</param>
/// <param name="IsPiloting">Whether navigation moves that camera.</param>
internal sealed record SceneCameraViewportState(Guid? ViewedCameraId, bool IsPiloting);

/// <summary>
/// Asks the window's active scene editor what its active viewport shows. No reply means no viewport.
/// </summary>
/// <param name="WindowId">The editor window owning the request.</param>
internal sealed class SceneCameraViewportStateRequestMessage(WindowId WindowId) : RequestMessage<SceneCameraViewportState>
{
    /// <summary>Gets the editor window owning the request.</summary>
    public WindowId WindowId { get; } = WindowId;
}
