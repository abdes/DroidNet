// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Settings;

/// <summary>Replaces the camera node the scene starts viewing through.</summary>
/// <remarks>Nothing reaches the engine: editor viewports keep their own cameras.</remarks>
/// <param name="Camera">The camera node, or <see langword="null"/> for the first camera in hierarchy order.</param>
public sealed record SetStartingCamera(NodeReference? Camera) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Set Starting Camera";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Camera is not { } camera)
        {
            return SceneEditValidation.Valid;
        }

        // A reference with a path names a node an instance brings in; that node is not part of the scene's own
        // nodes, so only the instance root can be checked until instanced nodes are resolvable here.
        var valid = scope.Scene.AllNodes.Any(node => node.Id == camera.Node
            && (camera.IsOwned
                ? node.Components.OfType<CameraComponent>().Any()
                : node.Components.OfType<SceneInstanceComponent>().Any()));
        return valid
            ? SceneEditValidation.Valid
            : SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.StartingCameraInvalid, "The starting camera must be a camera node of this scene, or a node of a scene instance in it."));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = scope.Scene.StartingCamera;
        scope.Scene.SetStartingCamera(this.Camera);
        return scope.Scene.StartingCamera == before
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new SetStartingCamera(before), [new SceneChange.StartingCameraChanged()]);
    }
}
