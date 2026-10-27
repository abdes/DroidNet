// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Which of an instance's changes <i>Apply to source</i> moves (scene instances 6.7): all of them, those of one node,
/// those of one component of a node, or the values of one changed row.
/// </summary>
public sealed class ChangeSelection
{
    private ChangeSelection(NodePath? node, string? componentKind, IReadOnlyList<PropertyId>? properties, PartId? part)
    {
        this.Node = node;
        this.ComponentKind = componentKind;
        this.Properties = properties;
        this.Part = part;
    }

    /// <summary>Gets the selection of every change of the instance.</summary>
    public static ChangeSelection Instance { get; } = new(node: null, componentKind: null, properties: null, part: null);

    /// <summary>Gets the node whose changes are selected, or <see langword="null"/> for every node of the instance.</summary>
    public NodePath? Node { get; }

    /// <summary>Gets the component kind whose changes are selected, or <see langword="null"/> for every change of the node.</summary>
    public string? ComponentKind { get; }

    /// <summary>Gets the properties whose changes are selected, or <see langword="null"/> for every change of the component.</summary>
    public IReadOnlyList<PropertyId>? Properties { get; }

    /// <summary>Gets the part the selected properties belong to, or <see langword="null"/> for properties of the component itself.</summary>
    public PartId? Part { get; }

    /// <summary>Selects every change of one node, its script parameters included.</summary>
    /// <param name="node">The node's path below the instance root.</param>
    /// <returns>The selection.</returns>
    public static ChangeSelection OfNode(NodePath node)
        => new(Require(node), componentKind: null, properties: null, part: null);

    /// <summary>Selects the changes of one component of a node.</summary>
    /// <param name="node">The node's path below the instance root.</param>
    /// <param name="componentKind">The component kind, such as <see cref="SceneSchema.Transform.Kind"/>.</param>
    /// <returns>The selection.</returns>
    public static ChangeSelection OfComponent(NodePath node, string componentKind)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(componentKind);
        return new(Require(node), componentKind, properties: null, part: null);
    }

    /// <summary>Selects the changes of some properties of a node, those one row shows, all of one component kind.</summary>
    /// <param name="node">The node's path below the instance root.</param>
    /// <param name="properties">The properties, such as the three axes of a position.</param>
    /// <param name="part">The part they belong to, or <see langword="null"/> for properties of the component itself.</param>
    /// <returns>The selection.</returns>
    /// <exception cref="ArgumentException">There is no property, or they are of several component kinds.</exception>
    public static ChangeSelection OfValues(NodePath node, IReadOnlyList<PropertyId> properties, PartId? part = null)
    {
        ArgumentNullException.ThrowIfNull(properties);
        if (properties.Count == 0 || properties.Select(static property => property.ComponentKind).Distinct(StringComparer.Ordinal).Skip(1).Any())
        {
            throw new ArgumentException("A row's values are one or more properties of one component kind.", nameof(properties));
        }

        return new(Require(node), properties[0].ComponentKind, [.. properties], part);
    }

    /// <summary>Gets what remains of a node's changes once some of them are taken away.</summary>
    /// <param name="changes">The node's changes.</param>
    /// <param name="taken">The changes taken away, from <see cref="Select"/>.</param>
    /// <returns>The remaining changes; empty when none remains.</returns>
    public static InstancedNodeChanges Without(InstancedNodeChanges changes, InstancedNodeChanges taken)
    {
        ArgumentNullException.ThrowIfNull(changes);
        ArgumentNullException.ThrowIfNull(taken);
        var remaining = taken.Components.Values
            .SelectMany(static component => component.All)
            .Aggregate(changes, static (all, change) => all.With(all.Of(change.Property.ComponentKind).Without(change.Property, change.Part)));
        return taken.Parameters
            .SelectMany(static slot => slot.Value.Keys.Select(name => (Slot: slot.Key, Name: name)))
            .Aggregate(remaining, static (all, parameter) => all.WithoutParameter(parameter.Slot, parameter.Name));
    }

    /// <summary>Gets the selected changes of one node.</summary>
    /// <param name="path">The node's path below the instance root.</param>
    /// <param name="changes">The node's changes.</param>
    /// <returns>The selected changes, with the node's names; empty when none is selected.</returns>
    public InstancedNodeChanges Select(NodePath path, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(changes);
        if (this.Node is { } node && !node.Equals(path))
        {
            return InstancedNodeChanges.None;
        }

        if (this.ComponentKind is not { } kind)
        {
            return changes;
        }

        var component = changes.Of(kind);
        var selected = this.Properties is { } properties
            ? properties
                .Select(property => component.Find(property, this.Part))
                .OfType<SourceOverride>()
                .Aggregate(SourceOverrides.ChangesOf(kind), static (all, change) => all.With(change))
            : component;
        return InstancedNodeChanges.None.With(selected).WithNames(changes.Names).WithCarriedFrom(changes.CarriedFrom);
    }

    private static NodePath Require(NodePath node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return node.IsEmpty ? throw new ArgumentException("A change names a node below the instance root.", nameof(node)) : node;
    }
}
