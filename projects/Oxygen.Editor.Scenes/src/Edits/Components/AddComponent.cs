// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Components;

/// <summary>Adds a component to a node, at a position among its components.</summary>
/// <remarks>
/// A node holds one component of each family: one geometry, one camera, one light, one local fog volume and one
/// Scripting component; every node already has its transform. The component's consequences, such as pointing a new
/// directional light down, are separate edits; <see cref="Nodes.SceneComponents.Add"/> batches them.
/// </remarks>
public sealed record AddComponent : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="AddComponent"/> class.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The component's data, with its id; the edit keeps its own copy.</param>
    /// <param name="index">The component's position among the node's components; past the end appends.</param>
    public AddComponent(NodeId node, ComponentData component, int index)
    {
        ArgumentNullException.ThrowIfNull(component);
        this.Node = node;
        this.Component = SerializationCopies.Copy(component);
        this.Index = index;
    }

    /// <summary>Gets the node.</summary>
    public NodeId Node { get; }

    /// <summary>Gets the component's data.</summary>
    public ComponentData Component { get; }

    /// <summary>Gets the component's position among the node's components.</summary>
    public int Index { get; }

    /// <inheritdoc/>
    public string Label => $"Add {this.Component.Name}";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var node = scope.FindNode(this.Node)!;
        if (this.Index < 0 || this.Index > node.Components.Count)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The component position is outside the node's components.", this.Node));
        }

        if (node.Components.Any(component => component.Id == this.Component.Id) || scope.Scene.AllNodes.Any(other => other.Components.Any(component => component.Id == this.Component.Id)))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentDuplicate, "The scene already has a component with this identity.", this.Node));
        }

        return ComponentFamilies.Conflict(node, this.Component) is { } conflict
            ? SceneEditValidation.Invalid(conflict with { Node = this.Node })
            : this.Component is DirectionalLightData light && DirectionalLightRules.SlotConflict(scope.Scene, new Dictionary<NodeId, DirectionalLightData> { [this.Node] = light }) is { } sun
            ? SceneEditValidation.Invalid(sun)
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var node = scope.FindNode(this.Node)!;
        var component = GameComponent.CreateAndHydrate(SerializationCopies.Copy(this.Component));
        _ = node.AddComponent(component);
        node.Components.Move(node.Components.Count - 1, this.Index);
        return new SceneEditApplication.Applied(
            new RemoveComponent(this.Node, component.Id),
            [new SceneChange.ComponentAdded(this.Node, component.Id)]);
    }
}
