// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Undoes <see cref="UnpackInstance"/>: the owned nodes it made leave the instance root, which takes its instance
/// component back with its changes, and the references to those nodes name the instance's nodes again.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Depth">How deep the instance was unpacked.</param>
/// <param name="Component">The instance component's data, with its ID, source and changes.</param>
/// <param name="Index">The instance component's position among the root's components.</param>
/// <param name="Copies">Each node of the instance and its copy.</param>
public sealed record RestoreInstance(NodeId Instance, InstanceUnpack Depth, ComponentData Component, int Index, IReadOnlyList<UnpackedNode> Copies) : ISceneEdit
{
    /// <summary>Gets the instance component's data; the edit keeps its own copy.</summary>
    public ComponentData Component { get; init; } = Component is null ? null! : SerializationCopies.Copy(Component);

    /// <summary>Gets each node of the instance and its copy.</summary>
    public IReadOnlyList<UnpackedNode> Copies { get; init; } = Copies is null ? null! : [.. Copies];

    /// <inheritdoc/>
    public string Label => this.Depth == InstanceUnpack.Complete ? "Unpack Instance Completely" : "Unpack Instance";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Component is not SceneInstanceComponentData || this.Copies is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Restoring an instance names its instance component and the copies of its nodes.", this.Instance));
        }

        var contents = this.ContentsOf(scope);
        if (contents.Count == 0 && this.Copies.Count != 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The nodes the instance was unpacked into no longer exist.", this.Instance));
        }

        var removal = contents.Count == 0 ? SceneEditValidation.Valid : ((ISceneEdit)new RemoveNodes(contents)).Validate(scope);
        return removal.IsValid ? ((ISceneEdit)new AddComponent(this.Instance, this.Component, this.Index)).Validate(scope) : removal;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var retargeted = UnpackedReferences.Retarget(scope, UnpackedReferences.Backward(this.Instance, this.Copies));
        var contents = this.ContentsOf(scope);
        var removed = contents.Count == 0 ? null : (SceneEditApplication.Applied)((ISceneEdit)new RemoveNodes(contents)).Apply(scope);
        var added = (SceneEditApplication.Applied)((ISceneEdit)new AddComponent(this.Instance, this.Component, this.Index)).Apply(scope);
        var instance = (SceneInstanceComponentData)this.Component;
        return new SceneEditApplication.Applied(
            new UnpackInstance(this.Instance, this.Depth)
            {
                Source = instance.Source,
                Changes = InstanceChangesJson.Read(instance.Changes),
                Contents = removed?.Revert is InsertNodes insert ? [.. contents.Select(id => insert.Inserts.Single(content => content.Node.Id == id).Node)] : [],
                Copies = this.Copies,
            },
            [.. retargeted, .. removed?.Changes ?? [], .. added.Changes]);
    }

    // The unpacked subtrees: the instance root's children that are copies, in order.
    private List<NodeId> ContentsOf(SceneEditScope scope)
    {
        var copies = this.Copies.Select(static copy => copy.Copy).ToHashSet();
        return [.. scope.FindNode(this.Instance)!.Hierarchy.Children.Select(static child => child.Id).Where(copies.Contains)];
    }
}
