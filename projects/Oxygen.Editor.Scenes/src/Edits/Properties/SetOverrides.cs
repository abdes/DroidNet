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
/// surface over the geometry asset's.
/// </summary>
/// <remarks>
/// Each write addresses a property <see cref="SceneSchema.Overridable"/> lists, on the node's component of that
/// property's kind, and on a part of the property's part kind when it has one. A value has the property's type and passes
/// its rule; a reset removes the override, so the source's value applies. Whether a part exists in the current asset is
/// checked when the edit is built (see <see cref="Assets.SurfaceEdits"/>), which also names the asset the part was chosen
/// from, so the edit refuses a node that has since changed it. An override of a part the asset no longer has is kept,
/// unresolved. The edit's inverse writes back each override as it was, whatever the asset.
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
    public IReadOnlyCollection<NodeId> Targets => [.. this.Writes.Keys];

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
                if ((Check(write) ?? CheckComponent(scope, node, write)) is { } invalid)
                {
                    return SceneEditValidation.Invalid(invalid with { Node = id });
                }
            }

            if (writes.GroupBy(static write => (write.Property, write.Part)).FirstOrDefault(static group => group.Count() > 1) is { } repeated)
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
                var component = ComponentOf(node, write.Property)!;
                var previous = component.Overrides.Find(write.Property, write.Part);
                var value = write.Value is AssetPath asset ? scope.Resolve(asset) : write.Value;
                var updated = value is null
                    ? component.Overrides.Without(write.Property, write.Part)
                    : component.Overrides.With(new SourceOverride(write.Property, write.Part, write.LayoutRevision, value));
                if (updated.Equals(component.Overrides))
                {
                    continue;
                }

                component.Overrides = updated;
                inverse.Insert(0, OverrideWrite.Restoring(write.Property, write.Part, previous));
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

    private static IOverridableComponent? ComponentOf(SceneNode? node, PropertyId property)
        => node?.Components.OfType<IOverridableComponent>()
            .SingleOrDefault(component => string.Equals(component.Overrides.ComponentKind, property.ComponentKind, StringComparison.Ordinal));

    private static SceneDiagnostic? CheckComponent(SceneEditScope scope, SceneNode? node, OverrideWrite write)
    {
        if (ComponentOf(node, write.Property) is not { } component)
        {
            return SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, $"The node has no {write.Property.ComponentKind} component.", property: write.Property);
        }

        return write is { PartOwner: { } owner, Part: { } part } && component.PartOwner(part.Kind) != scope.Resolve(owner)
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
