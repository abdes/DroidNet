// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Components;

/// <summary>Removes a component from a node; the transform cannot be removed.</summary>
/// <param name="Node">The node.</param>
/// <param name="Component">The component.</param>
public sealed record RemoveComponent(NodeId Node, ComponentId Component) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Remove Component";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return this.Find(scope) switch
        {
            null => SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The component no longer exists.", this.Node)),
            { } component when component.IsLocked || component is TransformComponent
                => SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentNotRemovable, "The transform component is locked and cannot be removed.", this.Node)),
            _ => SceneEditValidation.Valid,
        };
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var node = scope.FindNode(this.Node)!;
        var component = this.Find(scope)!;
        var index = node.Components.IndexOf(component);
        var data = component.Dehydrate();
        _ = node.RemoveComponent(component);
        return new SceneEditApplication.Applied(
            new AddComponent(this.Node, data, index),
            [new SceneChange.ComponentRemoved(this.Node, this.Component, SceneComponents.KindOf(component))]);
    }

    private GameComponent? Find(SceneEditScope scope)
        => scope.FindNode(this.Node)?.Components.FirstOrDefault(component => component.Id == this.Component);
}
