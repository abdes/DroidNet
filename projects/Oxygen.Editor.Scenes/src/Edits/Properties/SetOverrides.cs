// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>
/// Sets or resets overrides: the values nodes' components store over their sources, such as the material of a geometry
/// surface over the geometry asset's, and the changes a scene makes of the nodes an instance brings in.
/// </summary>
/// <remarks>
/// Each write addresses a property <see cref="SceneSchema.Overridable"/> lists, on the node's component of that
/// property's kind, and on a part of the property's part kind when it has one. A value has the property's type and passes
/// its rule; a reset removes the override, so the source's value applies. Whether a part exists in the current asset is
/// checked when the edit is built (see <see cref="Assets.SurfaceEdits"/>), which also names the asset the part was chosen
/// from, so the edit refuses a node that has since changed it. An override of a part the asset no longer has is kept,
/// unresolved. The edit's inverse writes back each override as it was, whatever the asset.
/// <para>
/// A write with a <see cref="OverrideWrite.Path"/> changes the node below the instance root that the path names: it sets
/// or removes the change in the root's <see cref="SceneInstanceComponent.Changes"/> (see
/// <see cref="InstanceChangeWrites"/>). Such a node and its ancestors, instanced or not, must be unlocked.
/// </para>
/// </remarks>
public sealed record SetOverrides : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetOverrides"/> class, with the same writes on each node.</summary>
    /// <param name="nodes">The nodes.</param>
    /// <param name="writes">The writes.</param>
    /// <param name="label">The history label.</param>
    public SetOverrides(IReadOnlyCollection<NodeId> nodes, IReadOnlyList<OverrideWrite> writes, string label)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(writes);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        this.Writes = nodes.Distinct().ToDictionary(static id => id, _ => (IReadOnlyList<OverrideWrite>)[.. writes]);
        this.Label = label;
    }

    /// <summary>Initializes a new instance of the <see cref="SetOverrides"/> class, with the writes of each node.</summary>
    /// <param name="writes">The writes of each node.</param>
    /// <param name="label">The history label.</param>
    [JsonConstructor]
    public SetOverrides(IReadOnlyDictionary<NodeId, IReadOnlyList<OverrideWrite>> writes, string label)
    {
        ArgumentNullException.ThrowIfNull(writes);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        this.Writes = writes.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<OverrideWrite>)[.. pair.Value]);
        this.Label = label;
    }

    /// <summary>Gets the writes of each node, applied in order.</summary>
    public IReadOnlyDictionary<NodeId, IReadOnlyList<OverrideWrite>> Writes { get; }

    /// <inheritdoc/>
    public string Label { get; }

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets
        => [.. this.Writes.Where(static pair => pair.Value.Any(static write => write.Path is null)).Select(static pair => pair.Key)];

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> InstancedTargets
        => [.. this.Writes
            .SelectMany(static pair => pair.Value.Where(static write => write.Path is not null).Select(write => new NodeReference(pair.Key, write.Path!)))
            .Distinct()];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Writes.Values.All(static writes => writes.Count == 0))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No override to edit."));
        }

        foreach (var (id, writes) in this.Writes)
        {
            var node = scope.FindNode(id);
            foreach (var write in writes)
            {
                var invalid = write.Path is { } path
                    ? InstanceChangeWrites.Check(scope, node, path, write)
                    : Check(write) ?? CheckComponent(scope, node, write);
                if (invalid is not null)
                {
                    return SceneEditValidation.Invalid(invalid with { Node = id });
                }
            }

            if (writes.GroupBy(static write => (write.Path?.ToString(), write.Property, write.Part)).FirstOrDefault(static group => group.Skip(1).Any()) is { } repeated)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, $"{repeated.Key.Property} is written twice on one part.", id, repeated.Key.Property));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var reverts = new Dictionary<NodeId, IReadOnlyList<OverrideWrite>>();
        var changes = new List<SceneChange>();
        foreach (var (id, writes) in this.Writes.Where(static pair => pair.Value.Count > 0))
        {
            var node = scope.FindNode(id)!;
            var inverse = new List<OverrideWrite>();
            var changed = new List<PropertyId>();
            foreach (var write in writes)
            {
                var resolved = write.Value is AssetPath asset ? write with { Value = scope.Resolve(asset) } : write;
                if (!Write(node, resolved, out var previous))
                {
                    continue;
                }

                inverse.Insert(0, OverrideWrite.Restoring(write.Property, write.Part, previous) with { Path = write.Path });
                if (!changed.Contains(write.Property))
                {
                    changed.Add(write.Property);
                }
            }

            if (inverse.Count > 0)
            {
                reverts[id] = inverse;
                changes.Add(new SceneChange.OverridesChanged(id, changed));
                scope.WarnIfDeleted(writes.Select(static write => write.Value).OfType<AssetPath>().Select(scope.Resolve), id);
            }
        }

        return new SceneEditApplication.Applied(reverts.Count == 0 ? this : new SetOverrides(reverts, this.Label), changes);
    }

    // Writes an override of the node's own component, or a change of a node its instance brings in.
    private static bool Write(SceneNode node, OverrideWrite write, out SourceOverride? previous)
    {
        if (write.Path is { } path)
        {
            return InstanceChangeWrites.Write(InstanceChangeWrites.InstanceOf(node)!, path, write, out previous);
        }

        var component = ComponentOf(node, write.Property)!;
        previous = component.Overrides.Find(write.Property, write.Part);
        var updated = write.Value is null
            ? component.Overrides.Without(write.Property, write.Part)
            : component.Overrides.With(new SourceOverride(write.Property, write.Part, write.LayoutRevision, write.Value));
        if (updated.Equals(component.Overrides))
        {
            return false;
        }

        component.Overrides = updated;
        return true;
    }

    private static IOverridableComponent? ComponentOf(SceneNode? node, PropertyId property)
        => node?.Components.OfType<IOverridableComponent>()
            .SingleOrDefault(component => string.Equals(component.Overrides.ComponentKind, property.ComponentKind, StringComparison.Ordinal));

    private static SceneDiagnostic? CheckComponent(SceneEditScope scope, SceneNode? node, OverrideWrite write)
    {
        if (ComponentOf(node, write.Property) is not { })
        {
            return SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, $"The node has no {write.Property.ComponentKind} component.", property: write.Property);
        }

        return write is { PartOwner: { } owner, Part: { } part } && PartOwners.Resolve(scope, node!, path: null, part.Kind) != scope.Resolve(owner)
            ? SceneDiagnostic.Error(SceneEditCodes.PartStale, "The node no longer uses the asset the part was chosen from.", property: write.Property)
            : null;
    }

    private static SceneDiagnostic? Check(OverrideWrite write)
    {
        // A deserialized write can still carry nulls.
        if (write.Property is not { } property || !SceneSchema.Overridable.TryGetValue(property, out var type))
        {
            return SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, $"{write.Property} has no source, so it cannot be overridden.", property: write.Property);
        }

        if (property.PartKind is null
            ? write.Part is not null
            : write.Part is not { } part || !string.Equals(part.Kind, property.PartKind, StringComparison.Ordinal) || part.Id == Guid.Empty)
        {
            return Invalid(property, property.PartKind is null ? $"{property} belongs to the component, not to a part." : $"{property} needs a {property.PartKind} part.");
        }

        if (write.Value is null)
        {
            return null;
        }

        if (!type.IsInstanceOfType(write.Value))
        {
            return Invalid(property, $"{property} takes a {type.Name}.");
        }

        if (property.PartKind is not null && !SourceOverrides.IsLayoutRevision(write.LayoutRevision))
        {
            return Invalid(property, "Select an existing part: the layout revision it was chosen from is missing or invalid.");
        }

        return OverrideRules.Check(property, write.Value) is { IsValid: false } refused
            ? SceneDiagnostic.Error(refused.Code, refused.Message, property: property)
            : null;

        static SceneDiagnostic Invalid(PropertyId property, string message) => SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, message, property: property);
    }
}
