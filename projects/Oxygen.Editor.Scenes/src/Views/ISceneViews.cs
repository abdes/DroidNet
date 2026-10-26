// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Views;

/// <summary>
/// The viewports that show a scene document: what a part asks of them without knowing the editor that owns them, such
/// as the Scene Explorer framing nodes or looking through a camera.
/// </summary>
/// <remarks>The scene editor implements it; a document no editor shows answers <see langword="false"/> and <see langword="null"/>.</remarks>
public interface ISceneViews
{
    /// <summary>Frames nodes in the active viewport of the editor that shows a scene document.</summary>
    /// <param name="document">The scene document.</param>
    /// <param name="nodes">The nodes to frame: nodes of the scene and nodes an instance brought in.</param>
    /// <returns><see langword="true"/> when a viewport framed them.</returns>
    public Task<bool> FrameAsync(DocumentId document, IReadOnlyList<NodeReference> nodes);

    /// <summary>Runs a camera command in the active viewport of the editor that shows a scene document.</summary>
    /// <param name="document">The scene document.</param>
    /// <param name="camera">The camera node.</param>
    /// <param name="command">What to do with the camera.</param>
    /// <returns><see langword="true"/> when the viewport ran it.</returns>
    public Task<bool> RunCameraCommandAsync(DocumentId document, NodeId camera, SceneCameraCommand command);

    /// <summary>Gets which camera the active viewport of a scene document's editor looks through.</summary>
    /// <param name="document">The scene document.</param>
    /// <returns>The viewport's camera state, or <see langword="null"/> when no editor shows the document.</returns>
    public SceneCameraViewState? CameraStateOf(DocumentId document);
}
