// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The changes a scene makes of one node an instance brings in: per component kind, the values it changes.
/// </summary>
/// <remarks>
/// The value is immutable. A component left without changes is removed.
/// </remarks>
public sealed class InstancedNodeChanges : IEquatable<InstancedNodeChanges>
{
    private InstancedNodeChanges(ImmutableDictionary<string, SourceOverrides> components, string? names, AssetPath? carriedFrom)
    {
        this.Components = components;
        this.Names = names;
        this.CarriedFrom = carriedFrom;
    }

    /// <summary>
    /// Gets the changes that change nothing.
    /// </summary>
    public static InstancedNodeChanges None { get; } = new(ImmutableDictionary<string, SourceOverrides>.Empty.WithComparers(StringComparer.Ordinal), names: null, carriedFrom: null);

    /// <summary>
    /// Gets the changes of each component kind that has any.
    /// </summary>
    public ImmutableDictionary<string, SourceOverrides> Components { get; }

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
    public bool IsEmpty => this.Components.IsEmpty;

    /// <summary>
    /// Gets every asset a change references.
    /// </summary>
    public IEnumerable<AssetPath> Assets
        => this.Components.OrderBy(static entry => entry.Key, StringComparer.Ordinal).SelectMany(static entry => entry.Value.Assets);

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
        return new(components, this.Names, this.CarriedFrom);
    }

    /// <summary>
    /// Records the node's last known names.
    /// </summary>
    /// <param name="names">The names, or <see langword="null"/> when they are not known.</param>
    /// <returns>The changes with the names.</returns>
    public InstancedNodeChanges WithNames(string? names) => new(this.Components, names, this.CarriedFrom);

    /// <summary>
    /// Records the source a swap carried the changes over from.
    /// </summary>
    /// <param name="source">The previous source, or <see langword="null"/> once the changes are made on the current one.</param>
    /// <returns>The changes with the source.</returns>
    public InstancedNodeChanges WithCarriedFrom(AssetPath? source) => new(this.Components, this.Names, source);

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

        return ReferenceEquals(components, this.Components) ? this : new(components, this.Names, this.CarriedFrom);
    }

    /// <inheritdoc/>
    public bool Equals(InstancedNodeChanges? other)
        => other is not null
            && string.Equals(this.Names, other.Names, StringComparison.Ordinal)
            && this.CarriedFrom == other.CarriedFrom
            && this.Components.Count == other.Components.Count
            && this.Components.All(entry => other.Components.TryGetValue(entry.Key, out var changes) && entry.Value.Equals(changes));

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as InstancedNodeChanges);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.Names, this.CarriedFrom, this.Components.Count);
}
