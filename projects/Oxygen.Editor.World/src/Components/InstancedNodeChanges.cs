// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The changes a scene makes of one node an instance brings in: per component kind, the values it changes, and per
/// script slot, the parameter values it changes.
/// </summary>
/// <remarks>
/// The value is immutable. A component or a slot left without changes is removed.
/// </remarks>
public sealed class InstancedNodeChanges : IEquatable<InstancedNodeChanges>
{
    private InstancedNodeChanges(ImmutableDictionary<string, SourceOverrides> components, ImmutableSortedDictionary<Guid, ImmutableSortedDictionary<string, ScriptValue>> parameters, string? names, AssetPath? carriedFrom)
    {
        this.Components = components;
        this.Parameters = parameters;
        this.Names = names;
        this.CarriedFrom = carriedFrom;
    }

    /// <summary>
    /// Gets the changes that change nothing.
    /// </summary>
    public static InstancedNodeChanges None { get; } = new(
        ImmutableDictionary<string, SourceOverrides>.Empty.WithComparers(StringComparer.Ordinal),
        ImmutableSortedDictionary<Guid, ImmutableSortedDictionary<string, ScriptValue>>.Empty,
        names: null,
        carriedFrom: null);

    /// <summary>
    /// Gets the changes of each component kind that has any.
    /// </summary>
    public ImmutableDictionary<string, SourceOverrides> Components { get; }

    /// <summary>
    /// Gets the script parameter values the scene changes, by the identity of the instanced node's script slot and the
    /// parameter's name.
    /// </summary>
    public ImmutableSortedDictionary<Guid, ImmutableSortedDictionary<string, ScriptValue>> Parameters { get; }

    /// <summary>
    /// Gets the node's last known names, one per nesting level in the anchor notation, joined by <c>/</c>, or
    /// <see langword="null"/> when they are not known yet.
    /// </summary>
    public string? Names { get; }

    /// <summary>
    /// Gets the source a swap carried these changes over from, or <see langword="null"/> when they were made on the current
    /// source.
    /// </summary>
    public AssetPath? CarriedFrom { get; }

    /// <summary>
    /// Gets a value indicating whether nothing is changed.
    /// </summary>
    public bool IsEmpty => this.Components.IsEmpty && this.Parameters.IsEmpty;

    /// <summary>
    /// Gets every asset a change references.
    /// </summary>
    public IEnumerable<AssetPath> Assets
        => this.Components.OrderBy(static entry => entry.Key, StringComparer.Ordinal).SelectMany(static entry => entry.Value.Assets)
            .Concat(this.Parameters.Values.SelectMany(static values => values.Values).Select(static value => value.Asset).OfType<AssetPath>());

    /// <summary>
    /// Gets the changes of one component kind.
    /// </summary>
    /// <param name="componentKind">The component kind.</param>
    /// <returns>The changes; empty when the component is not changed.</returns>
    public SourceOverrides Of(string componentKind)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(componentKind);
        return this.Components.GetValueOrDefault(componentKind) ?? SourceOverrides.ChangesOf(componentKind);
    }

    /// <summary>
    /// Replaces the changes of one component kind.
    /// </summary>
    /// <param name="changes">The component's changes, from <see cref="SourceOverrides.ChangesOf"/>; empty removes them.</param>
    /// <returns>The changes with the component's.</returns>
    /// <exception cref="ArgumentException">The changes are a source's overrides, not an instanced node's changes.</exception>
    public InstancedNodeChanges With(SourceOverrides changes)
    {
        ArgumentNullException.ThrowIfNull(changes);
        if (!changes.IsInstanceChanges)
        {
            throw new ArgumentException("The changes of an instanced node are made with SourceOverrides.ChangesOf.", nameof(changes));
        }

        var components = changes.IsEmpty
            ? this.Components.Remove(changes.ComponentKind)
            : this.Components.SetItem(changes.ComponentKind, changes);
        return new(components, this.Parameters, this.Names, this.CarriedFrom);
    }

    /// <summary>
    /// Changes the value of one script parameter.
    /// </summary>
    /// <param name="slot">The identity of the instanced node's script slot.</param>
    /// <param name="name">The parameter's name.</param>
    /// <param name="value">The value.</param>
    /// <returns>The changes with the parameter's value.</returns>
    public InstancedNodeChanges WithParameter(Guid slot, string name, ScriptValue value)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentNullException.ThrowIfNull(value);
        if (slot == Guid.Empty)
        {
            throw new ArgumentException("A parameter change names its script slot.", nameof(slot));
        }

        var values = this.Parameters.GetValueOrDefault(slot) ?? ImmutableSortedDictionary.Create<string, ScriptValue>(StringComparer.Ordinal);
        return new(this.Components, this.Parameters.SetItem(slot, values.SetItem(name, value)), this.Names, this.CarriedFrom);
    }

    /// <summary>
    /// Removes the change of one script parameter, so the source's value applies.
    /// </summary>
    /// <param name="slot">The identity of the instanced node's script slot.</param>
    /// <param name="name">The parameter's name.</param>
    /// <returns>The changes without the parameter's.</returns>
    public InstancedNodeChanges WithoutParameter(Guid slot, string name)
    {
        if (this.Parameters.GetValueOrDefault(slot) is not { } values || !values.ContainsKey(name))
        {
            return this;
        }

        var remaining = values.Remove(name);
        return new(this.Components, remaining.IsEmpty ? this.Parameters.Remove(slot) : this.Parameters.SetItem(slot, remaining), this.Names, this.CarriedFrom);
    }

    /// <summary>
    /// Re-points node-valued parameter changes at the copies of the nodes they name.
    /// </summary>
    /// <param name="copies">Maps each copied node identity to the identity of its copy.</param>
    /// <returns>The changes with re-pointed node values.</returns>
    public InstancedNodeChanges MapNodes(IReadOnlyDictionary<NodeId, NodeId> copies)
    {
        ArgumentNullException.ThrowIfNull(copies);
        return this.MapParameters(value => value.Kind == ScriptValueKind.NodeValue && value.Node is { } reference && copies.TryGetValue(reference.Node, out var copy)
            ? ScriptValue.FromNode(reference with { Node = copy })
            : value);
    }

    /// <summary>
    /// Re-points node-valued parameter changes at other nodes, as when an unpacked instance's nodes become the scene's.
    /// </summary>
    /// <param name="map">The node a reference names now, or <see langword="null"/> to keep the reference.</param>
    /// <returns>The changes with re-pointed node values.</returns>
    public InstancedNodeChanges MapNodes(Func<NodeReference, NodeReference?> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        return this.MapParameters(value => value.Kind == ScriptValueKind.NodeValue && value.Node is { } reference && map(reference) is { } target && target != reference
            ? ScriptValue.FromNode(target)
            : value);
    }

    /// <summary>
    /// Records the node's last known names.
    /// </summary>
    /// <param name="names">The names, or <see langword="null"/> when they are not known.</param>
    /// <returns>The changes with the names.</returns>
    public InstancedNodeChanges WithNames(string? names) => new(this.Components, this.Parameters, names, this.CarriedFrom);

    /// <summary>
    /// Records the source a swap carried the changes over from.
    /// </summary>
    /// <param name="source">The previous source, or <see langword="null"/> once the changes are made on the current one.</param>
    /// <returns>The changes with the source.</returns>
    public InstancedNodeChanges WithCarriedFrom(AssetPath? source) => new(this.Components, this.Parameters, this.Names, source);

    /// <summary>
    /// Replaces every asset a change references.
    /// </summary>
    /// <param name="map">Returns the asset that replaces one, or the same asset.</param>
    /// <returns>The changes with the replaced assets.</returns>
    public InstancedNodeChanges MapAssets(Func<AssetPath, AssetPath> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var components = this.Components;
        foreach (var (kind, changes) in this.Components)
        {
            var mapped = changes.MapAssets(map);
            components = ReferenceEquals(mapped, changes) ? components : components.SetItem(kind, mapped);
        }

        var withComponents = ReferenceEquals(components, this.Components) ? this : new(components, this.Parameters, this.Names, this.CarriedFrom);
        return withComponents.MapParameters(value => value.Asset is { } asset && map(asset) is var target && target != asset ? ScriptValue.FromAsset(target) : value);
    }

    /// <inheritdoc/>
    public bool Equals(InstancedNodeChanges? other)
        => other is not null
            && string.Equals(this.Names, other.Names, StringComparison.Ordinal)
            && this.CarriedFrom == other.CarriedFrom
            && this.Components.Count == other.Components.Count
            && this.Components.All(entry => other.Components.TryGetValue(entry.Key, out var changes) && entry.Value.Equals(changes))
            && this.Parameters.Count == other.Parameters.Count
            && this.Parameters.All(entry => other.Parameters.TryGetValue(entry.Key, out var values)
                && entry.Value.Count == values.Count
                && entry.Value.All(value => values.TryGetValue(value.Key, out var held) && value.Value.Equals(held)));

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as InstancedNodeChanges);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.Names, this.CarriedFrom, this.Components.Count, this.Parameters.Count);

    // Replaces each parameter value; the same instance when no value changes.
    private InstancedNodeChanges MapParameters(Func<ScriptValue, ScriptValue> map)
    {
        var parameters = this.Parameters;
        foreach (var (slot, values) in this.Parameters)
        {
            var mapped = values;
            foreach (var (name, value) in values)
            {
                var replaced = map(value);
                mapped = ReferenceEquals(replaced, value) ? mapped : mapped.SetItem(name, replaced);
            }

            parameters = ReferenceEquals(mapped, values) ? parameters : parameters.SetItem(slot, mapped);
        }

        return ReferenceEquals(parameters, this.Parameters) ? this : new(this.Components, parameters, this.Names, this.CarriedFrom);
    }
}
