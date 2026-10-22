// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Frozen;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// An immutable, sparse set of <c>(PropertyId, value)</c> entries: the values one edit writes.
/// </summary>
/// <remarks>
/// <para>
/// Absence is encoded by absence in the map. Values are the descriptors' value types, stored boxed; the typed accessor
/// <see cref="GetTyped{T}"/> performs the cast.
/// </para>
/// <para>
/// An edit never changes once built, so an edit record can own it and share it safely. Build one with a
/// <see cref="Builder"/>, <see cref="SingleEdit{T}"/> or <see cref="With{T}"/>.
/// </para>
/// </remarks>
public sealed class PropertyEdit : IEnumerable<KeyValuePair<PropertyId, object?>>
{
    private readonly FrozenDictionary<PropertyId, object?> entries;

    private PropertyEdit(IDictionary<PropertyId, object?> entries)
    {
        this.entries = entries.ToFrozenDictionary();
    }

    /// <summary>
    /// Gets an empty edit.
    /// </summary>
    public static PropertyEdit Empty { get; } = new(new Dictionary<PropertyId, object?>());

    /// <summary>
    /// Gets the number of entries in the edit.
    /// </summary>
    public int Count => this.entries.Count;

    /// <summary>
    /// Gets the property ids present in the edit.
    /// </summary>
    public IReadOnlyCollection<PropertyId> Ids => this.entries.Keys;

    /// <summary>
    /// Creates a single-entry edit.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The value.</param>
    /// <returns>The new edit.</returns>
    public static PropertyEdit SingleEdit<T>(PropertyId<T> id, T value)
    {
        ArgumentNullException.ThrowIfNull(id);
        return new(new Dictionary<PropertyId, object?> { [id.Id] = value });
    }

    /// <summary>
    /// Creates an edit from raw entries; each value must be its descriptor's value type.
    /// </summary>
    /// <param name="entries">The entries.</param>
    /// <returns>The new edit.</returns>
    public static PropertyEdit Of(IEnumerable<KeyValuePair<PropertyId, object?>> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var map = new Dictionary<PropertyId, object?>();
        foreach (var (id, value) in entries)
        {
            map[id] = value;
        }

        return new(map);
    }

    /// <summary>
    /// Returns an edit with one more entry, or with an entry's value replaced.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The value.</param>
    /// <returns>The new edit.</returns>
    public PropertyEdit With<T>(PropertyId<T> id, T value)
    {
        ArgumentNullException.ThrowIfNull(id);
        return new(new Dictionary<PropertyId, object?>(this.entries) { [id.Id] = value });
    }

    /// <summary>
    /// Tries to retrieve a typed value.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The retrieved value, or <c>default</c> if absent.</param>
    /// <returns><see langword="true"/> when the entry is present.</returns>
    public bool GetTyped<T>(PropertyId<T> id, out T value)
    {
        ArgumentNullException.ThrowIfNull(id);
        if (this.entries.TryGetValue(id.Id, out var raw) && raw is T typed)
        {
            value = typed;
            return true;
        }

        value = default!;
        return false;
    }

    /// <summary>
    /// Tries to retrieve the raw value for a property id.
    /// </summary>
    /// <param name="id">The property id.</param>
    /// <param name="value">The retrieved value, or <c>null</c> when absent.</param>
    /// <returns><see langword="true"/> when the entry is present.</returns>
    public bool TryGetRaw(PropertyId id, out object? value) => this.entries.TryGetValue(id, out value);

    /// <summary>
    /// Returns <see langword="true"/> when the edit contains the given property id.
    /// </summary>
    /// <param name="id">The property id.</param>
    /// <returns><see langword="true"/> when the entry is present.</returns>
    public bool Contains(PropertyId id) => this.entries.ContainsKey(id);

    /// <summary>
    /// Builds a new edit that contains only the entries present in <paramref name="other"/>'s
    /// id set, taking values from this edit.
    /// </summary>
    /// <param name="other">The reference edit, used as the id mask.</param>
    /// <returns>The intersected edit.</returns>
    public PropertyEdit IntersectIds(PropertyEdit other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return Of(this.entries.Where(entry => other.entries.ContainsKey(entry.Key)));
    }

    /// <summary>
    /// Tests whether another edit holds the same entries with equal values.
    /// </summary>
    /// <param name="other">The other edit.</param>
    /// <returns><see langword="true"/> when both write the same values.</returns>
    public bool HasSameValues(PropertyEdit other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return this.Count == other.Count
            && this.entries.All(entry => other.entries.TryGetValue(entry.Key, out var value) && Equals(entry.Value, value));
    }

    /// <inheritdoc />
    public IEnumerator<KeyValuePair<PropertyId, object?>> GetEnumerator() => ((IEnumerable<KeyValuePair<PropertyId, object?>>)this.entries).GetEnumerator();

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <inheritdoc />
    public override string ToString() => $"PropertyEdit[{this.Count}]: {string.Join(", ", this.entries.Keys.Select(static id => id.Qualified()))}";

    /// <summary>
    /// Collects entries for a new <see cref="PropertyEdit"/>.
    /// </summary>
    public sealed class Builder
    {
        private readonly Dictionary<PropertyId, object?> entries = [];

        /// <summary>
        /// Gets the number of entries collected so far.
        /// </summary>
        public int Count => this.entries.Count;

        /// <summary>
        /// Adds or replaces a typed entry.
        /// </summary>
        /// <typeparam name="T">The value type.</typeparam>
        /// <param name="id">The property id.</param>
        /// <param name="value">The value.</param>
        /// <returns>This builder.</returns>
        public Builder Set<T>(PropertyId<T> id, T value)
        {
            ArgumentNullException.ThrowIfNull(id);
            this.entries[id.Id] = value;
            return this;
        }

        /// <summary>
        /// Adds or replaces a raw entry; the value must be its descriptor's value type.
        /// </summary>
        /// <param name="id">The property id.</param>
        /// <param name="value">The value, boxed.</param>
        /// <returns>This builder.</returns>
        public Builder SetRaw(PropertyId id, object? value)
        {
            ArgumentNullException.ThrowIfNull(id);
            this.entries[id] = value;
            return this;
        }

        /// <summary>
        /// Builds the edit.
        /// </summary>
        /// <returns>The edit.</returns>
        public PropertyEdit Build() => new(this.entries);
    }
}
