// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Unpacks an instance (scene instances 6.8, 8.3): owned nodes with the resolved values of the instance's nodes replace
/// the instance root's <see cref="SceneInstanceComponent"/>, first among its children, and every reference to an
/// unpacked node names its copy: the starting camera, a hidden or locked node, a script's or a change's node value.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Depth">Whether the instances the source holds stay instances or are unpacked too.</param>
/// <remarks>
/// <see cref="InstanceConversion.UnpackAsync"/> builds the edit from the instance's source. It is refused with
/// <see cref="SceneEditCodes.InstanceUnavailable"/> when the instance's source or changes are no longer those it was
/// built from. Changes of nodes the source does not hold are dropped and listed in a
/// <see cref="SceneEditCodes.NodePathUnresolved"/> warning.
/// </remarks>
public sealed record UnpackInstance(NodeId Instance, InstanceUnpack Depth) : ISceneEdit
{
    /// <summary>Gets the instance's source the contents were made from.</summary>
    public required AssetPath Source { get; init; }

    /// <summary>Gets the instance's changes the contents were made with.</summary>
    public required InstanceChanges Changes { get; init; }

    /// <summary>Gets the owned node subtrees that go under the instance root, in order; the edit keeps its own copy.</summary>
    public required IReadOnlyList<SceneNodeData> Contents
    {
        get;
        init => field = value is null ? null! : [.. value.Select(SerializationCopies.Copy)];
    }

    /// <summary>Gets each node of the instance the contents hold a copy of.</summary>
    public required IReadOnlyList<UnpackedNode> Copies
    {
        get;
        init => field = value is null ? null! : [.. value];
    }

    /// <inheritdoc/>
    public string Label => this.Depth == InstanceUnpack.Complete ? "Unpack Instance Completely" : "Unpack Instance";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Source is null || this.Changes is null || this.Contents is null || this.Copies is null || !Enum.IsDefined(this.Depth))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "An unpack names its instance's source, changes, contents and copies.", this.Instance));
        }

        var node = scope.FindNode(this.Instance)!;
        if (node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so there is nothing to unpack.", this.Instance));
        }

        if (scope.Resolve(this.Source) != instance.Source || !this.Changes.MapAssets(asset => scope.Resolve(asset)!).Equals(instance.Changes))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(
                SceneEditCodes.InstanceUnavailable,
                $"'{node.Metadata.Name}' changed while its contents were read. Unpack it again.",
                this.Instance));
        }

        var inserted = this.Contents.SelectMany(static content => SceneGraph.IdsOf(content).Nodes).ToHashSet();
        if (this.Copies.Any(copy => copy?.Path is not { IsEmpty: false } || !inserted.Contains(copy.Copy))
            || this.Copies.DistinctBy(static copy => copy.Path).Take(this.Copies.Count + 1).Count() != this.Copies.Count)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Each node an unpack copies has one copy among its contents.", this.Instance));
        }

        return ((ISceneEdit)this.Insert()).Validate(scope);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var node = scope.FindNode(this.Instance)!;
        var instance = node.Components.OfType<SceneInstanceComponent>().First();
        var forward = UnpackedReferences.Forward(this.Instance, this.Copies);
        var dropped = instance.Changes.Ordered.Where(change => forward(new NodeReference(this.Instance, change.Key)) is null).ToList();
        var removed = (SceneEditApplication.Applied)((ISceneEdit)new RemoveComponent(this.Instance, instance.Id)).Apply(scope);
        var inserted = (SceneEditApplication.Applied)((ISceneEdit)this.Insert()).Apply(scope);
        var retargeted = UnpackedReferences.Retarget(scope, forward);
        if (dropped.Count != 0)
        {
            scope.Warn([SceneDiagnostic.Warning(
                SceneEditCodes.NodePathUnresolved,
                $"{instance.Source.Name} does not hold the nodes of {dropped.Count} change(s) of '{node.Metadata.Name}', which were dropped: {string.Join(", ", dropped.Select(static change => change.Value.Names ?? change.Key.ToString()))}.",
                this.Instance)]);
        }

        var component = (AddComponent)removed.Revert;
        return new SceneEditApplication.Applied(
            new RestoreInstance(this.Instance, this.Depth, component.Component, component.Index, this.Copies),
            [.. removed.Changes, .. inserted.Changes, .. retargeted]);
    }

    // The contents go first under the instance root, in order, in the graph and in the explorer. Each insert's place is
    // checked against the children the root has before any of them, so all go to the front, last first.
    private InsertNodes Insert()
        => new([.. this.Contents.Reverse().Select(content => new NodeInsert(content, new NodePosition(this.Instance, 0), LayoutPosition.UnderNode(this.Instance, 0)))], this.Label);
}
