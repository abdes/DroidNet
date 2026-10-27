// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Viewports;

/// <summary>The editor of a document that shows a scene, as <see cref="SceneViews"/> reaches its viewports.</summary>
public interface ISceneView
{
    /// <summary>Frames nodes in the editor's focused viewport; no nodes frame the whole scene.</summary>
    /// <param name="nodes">The nodes to frame.</param>
    /// <returns>Whether a viewport framed them.</returns>
    public Task<bool> FrameNodesAsync(IReadOnlyList<NodeReference> nodes);

    /// <summary>Runs a camera command on a camera node of the scene in the editor's focused viewport.</summary>
    /// <param name="nodeId">The camera node.</param>
    /// <param name="command">The command.</param>
    /// <returns>Whether the command ran.</returns>
    public Task<bool> RunCameraCommandAsync(NodeId nodeId, SceneCameraCommand command);

    /// <summary>Gets which camera the editor's focused viewport looks through, and whether it pilots it.</summary>
    /// <returns>The state, or <see langword="null"/> when the editor has no viewport.</returns>
    public SceneCameraViewState? CameraViewState();

    /// <summary>Places an instance of a model or a scene in the editor's scene, and selects it.</summary>
    /// <param name="source">The model's source file or the authored scene.</param>
    /// <returns>Whether the instance was placed.</returns>
    public Task<bool> AddInstanceAsync(AssetPath source);
}
