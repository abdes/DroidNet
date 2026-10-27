// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Swaps an instance's source for another model or authored scene (scene instances 6.5, 8.3): the instance root keeps
/// its own components, and the instance's changes are kept by name, discarded or replaced, as <see cref="Swap"/> says.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Source">The new source: a model's source file or an authored scene.</param>
/// <param name="Swap">What happens to the instance's changes.</param>
/// <remarks>
/// A source that is or instances the scene is refused with <see cref="SceneEditCodes.InstanceCycle"/>. Keeping changes
/// by name needs the new source's structure; while it is not read, the swap is refused with
/// <see cref="SceneEditCodes.InstanceUnavailable"/>.
/// </remarks>
public sealed record SetInstanceSource(NodeId Instance, AssetPath Source, InstanceSwap Swap) : ISceneEdit
{
    /// <summary>Gets the changes the instance takes with <see cref="InstanceSwap.Replace"/>; unused otherwise.</summary>
    public InstanceChanges? Changes { get; init; }

    /// <inheritdoc/>
    public string Label => "Swap Instance Source";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Source is null || scope.Resolve(this.Source) is not { } source || AssetFileTypes.SceneProductOf(source) is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, "An instance's source is a model or an authored scene.", this.Instance));
        }

        if (InstanceOf(scope, this.Instance) is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no source to swap.", this.Instance));
        }

        if (!Enum.IsDefined(this.Swap) || (this.Swap == InstanceSwap.Replace && this.Changes is null))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, "A swap keeps the changes by name, discards them, or replaces them with the changes it carries.", this.Instance));
        }

        if (scope.CheckInstanceSource(source, this.Instance) is { } cycle)
        {
            return SceneEditValidation.Invalid(cycle);
        }

        return this.Swap == InstanceSwap.KeepByName && !instance.Changes.IsEmpty && source != instance.Source && scope.StructureOf?.Invoke(source) is null
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(
                SceneEditCodes.InstanceUnavailable,
                $"The contents of {source.Name} are not available yet, so the changes cannot be found in it by name. Swap again once it is cooked, or discard the changes.",
                this.Instance))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var instance = InstanceOf(scope, this.Instance)!;
        var source = scope.Resolve(this.Source)!;
        var (beforeSource, beforeChanges) = (instance.Source, instance.Changes);
        var changes = this.Swap switch
        {
            InstanceSwap.Replace => this.Changes!.MapAssets(asset => scope.Resolve(asset)!),
            InstanceSwap.Discard => InstanceChanges.None,
            _ when beforeChanges.IsEmpty || source == beforeSource => beforeChanges,
            _ => InstanceNamePaths.Carry(beforeChanges, beforeSource, scope.StructureOf?.Invoke(beforeSource), scope.StructureOf!.Invoke(source)!),
        };
        if (source == beforeSource && changes.Equals(beforeChanges))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        instance.Source = source;
        instance.Changes = changes;
        scope.WarnIfDeleted([source, .. changes.Assets], this.Instance);
        var changed = new List<SceneChange>();
        if (source != beforeSource)
        {
            changed.Add(new SceneChange.InstanceSourceChanged(this.Instance));
        }

        if (!changes.Equals(beforeChanges))
        {
            changed.Add(new SceneChange.OverridesChanged(this.Instance, [.. PropertiesOf(beforeChanges).Union(PropertiesOf(changes))]));
        }

        return new SceneEditApplication.Applied(
            new SetInstanceSource(this.Instance, beforeSource, InstanceSwap.Replace) { Changes = beforeChanges },
            changed);
    }

    private static SceneInstanceComponent? InstanceOf(SceneEditScope scope, NodeId node)
        => scope.FindNode(node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    private static IEnumerable<PropertyId> PropertiesOf(InstanceChanges changes)
        => changes.Nodes.Values.SelectMany(static node => node.Components.Values).SelectMany(static component => component.All).Select(static value => value.Property).Distinct();
}
