// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Generic;
using System.Linq;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Generic edit map: a sparse set of <c>(PropertyId, value)</c> entries.
/// </summary>
/// <remarks>
/// <para>
/// This replaces per-component edit records (e.g. <c>TransformEdit</c>'s
/// 12 <c>Optional&lt;T&gt;</c> fields). Absence is encoded by absence in
/// the map.
/// </para>
/// <para>
/// Entries are stored boxed; the typed accessor <see cref="GetTyped{T}"/>
/// performs the cast. The owning <see cref="PropertyDescriptor{T}"/>
/// provides type-safety at registration time, so untyped writes here are
/// only used inside the schema layer.
/// </para>
/// </remarks>
public sealed class PropertyEdit : IEnumerable<KeyValuePair<PropertyId, object?>>
{
    private readonly Dictionary<PropertyId, object?> entries;

    /// <summary>
    /// Initializes a new, empty edit.
    /// </summary>
    public PropertyEdit()
    {
        this.entries = [];
    }

    private PropertyEdit(Dictionary<PropertyId, object?> entries)
    {
        this.entries = entries;
    }

    /// <summary>
    /// Gets the number of entries in the edit.
    /// </summary>
    public int Count => this.entries.Count;

    /// <summary>
    /// Gets the property ids present in the edit.
    /// </summary>
    public IReadOnlyCollection<PropertyId> Ids => this.entries.Keys;

    /// <summary>
    /// An empty edit. Convenient for cancelled or no-op operations.
    /// </summary>
    public static PropertyEdit Empty { get; } = new();

    /// <summary>
    /// Creates a single-entry edit.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The value.</param>
    /// <returns>The new edit.</returns>
    public static PropertyEdit Single<T>(PropertyId<T> id, T value)
    {
        var edit = new PropertyEdit();
        edit.Set(id, value);
        return edit;
    }

    /// <summary>
    /// Adds or replaces a typed entry.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The value.</param>
    public void Set<T>(PropertyId<T> id, T value) => this.entries[id.Id] = value;

    /// <summary>
    /// Adds or replaces a raw entry. Internal escape hatch for the schema
    /// layer; prefer <see cref="Set{T}"/>.
    /// </summary>
    /// <param name="id">The property id.</param>
    /// <param name="value">The value, boxed.</param>
    internal void SetRaw(PropertyId id, object? value) => this.entries[id] = value;

    /// <summary>
    /// Tries to retrieve a typed value.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="id">The property id.</param>
    /// <param name="value">The retrieved value, or <c>default</c> if absent.</param>
    /// <returns><see langword="true"/> when the entry is present.</returns>
    public bool GetTyped<T>(PropertyId<T> id, out T value)
    {
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
        var result = new PropertyEdit();
        foreach (var id in other.entries.Keys)
        {
            if (this.entries.TryGetValue(id, out var value))
            {
                result.entries[id] = value;
            }
        }

        return result;
    }

    /// <summary>
    /// Returns a shallow copy of this edit.
    /// </summary>
    /// <returns>The cloned edit.</returns>
    public PropertyEdit Clone() => new(new Dictionary<PropertyId, object?>(this.entries));

    /// <inheritdoc />
    public IEnumerator<KeyValuePair<PropertyId, object?>> GetEnumerator() => this.entries.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <inheritdoc />
    public override string ToString() => $"PropertyEdit[{this.Count}]: {string.Join(", ", this.entries.Keys.Select(static id => id.Qualified()))}";
}
