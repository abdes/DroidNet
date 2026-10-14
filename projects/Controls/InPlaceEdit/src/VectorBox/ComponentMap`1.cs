// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Controls;

/// <summary>Per-component settings of a <see cref="VectorBox"/> that apply as soon as they change.</summary>
/// <typeparam name="T">The setting type.</typeparam>
/// <param name="changed">Applies the settings to the component editors.</param>
internal sealed class ComponentMap<T>(Action changed) : IDictionary<Component, T>
{
    private readonly Dictionary<Component, T> values = [];

    /// <inheritdoc/>
    public ICollection<Component> Keys => this.values.Keys;

    /// <inheritdoc/>
    public ICollection<T> Values => this.values.Values;

    /// <inheritdoc/>
    public int Count => this.values.Count;

    /// <inheritdoc/>
    public bool IsReadOnly => false;

    /// <inheritdoc/>
    public T this[Component key]
    {
        get => this.values[key];
        set
        {
            this.values[key] = value;
            changed();
        }
    }

    /// <inheritdoc/>
    public void Add(Component key, T value)
    {
        this.values.Add(key, value);
        changed();
    }

    /// <inheritdoc/>
    public void Add(KeyValuePair<Component, T> item) => this.Add(item.Key, item.Value);

    /// <inheritdoc/>
    public bool Remove(Component key)
    {
        var removed = this.values.Remove(key);
        if (removed)
        {
            changed();
        }

        return removed;
    }

    /// <inheritdoc/>
    public bool Remove(KeyValuePair<Component, T> item)
    {
        var removed = ((ICollection<KeyValuePair<Component, T>>)this.values).Remove(item);
        if (removed)
        {
            changed();
        }

        return removed;
    }

    /// <inheritdoc/>
    public void Clear()
    {
        if (this.values.Count > 0)
        {
            this.values.Clear();
            changed();
        }
    }

    /// <summary>Gets the setting of a component, or a fallback when it has none.</summary>
    /// <param name="key">The component.</param>
    /// <param name="fallback">The value to use when the component has no setting.</param>
    /// <returns>The component's setting, or <paramref name="fallback"/>.</returns>
    public T GetValueOrDefault(Component key, T fallback) => this.values.GetValueOrDefault(key, fallback);

    /// <inheritdoc/>
    public bool ContainsKey(Component key) => this.values.ContainsKey(key);

    /// <inheritdoc/>
    public bool Contains(KeyValuePair<Component, T> item) => ((ICollection<KeyValuePair<Component, T>>)this.values).Contains(item);

    /// <inheritdoc/>
    public bool TryGetValue(Component key, [MaybeNullWhen(false)] out T value) => this.values.TryGetValue(key, out value);

    /// <inheritdoc/>
    public void CopyTo(KeyValuePair<Component, T>[] array, int arrayIndex) => ((ICollection<KeyValuePair<Component, T>>)this.values).CopyTo(array, arrayIndex);

    /// <inheritdoc/>
    public IEnumerator<KeyValuePair<Component, T>> GetEnumerator() => this.values.GetEnumerator();

    /// <inheritdoc/>
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
