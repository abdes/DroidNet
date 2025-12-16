// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections;
using System.Collections.Generic;

namespace DroidNet.Collections;

/// <inheritdoc cref="FilteredObservableCollection{T}" />
public sealed partial class FilteredObservableCollection<T>
    where T : class, IEquatable<T>
{
    /// <inheritdoc />
    bool ICollection<T>.IsReadOnly => true;

    /// <inheritdoc />
    bool IList.IsReadOnly => true;

    /// <inheritdoc />
    bool IList.IsFixedSize => false;

    /// <inheritdoc />
    int ICollection.Count => this.Count;

    /// <inheritdoc />
    object ICollection.SyncRoot => this;

    /// <inheritdoc />
    bool ICollection.IsSynchronized => false;

    /// <inheritdoc />
    object? IList.this[int index]
    {
        get => this[index];
        set => throw new NotSupportedException();
    }

    /// <inheritdoc />
    T IList<T>.this[int index]
    {
        get => this[index];
        set => throw new NotSupportedException();
    }

    /// <inheritdoc />
    public void CopyTo(T[] array, int arrayIndex)
    {
        foreach (var item in this)
        {
            array[arrayIndex++] = item;
        }
    }

    /// <inheritdoc />
    public void Add(T item) => throw new NotSupportedException();

    /// <inheritdoc />
    public void Clear() => throw new NotSupportedException();

    /// <inheritdoc />
    public bool Remove(T item) => throw new NotSupportedException();

    /// <inheritdoc />
    public void Insert(int index, T item) => throw new NotSupportedException();

    /// <inheritdoc />
    public void RemoveAt(int index) => throw new NotSupportedException();

    /// <inheritdoc />
    int IList.Add(object? value) => throw new NotSupportedException();

    /// <inheritdoc />
    void IList.Clear() => throw new NotSupportedException();

    /// <inheritdoc />
    bool IList.Contains(object? value) => value is T t && this.Contains(t);

    /// <inheritdoc />
    int IList.IndexOf(object? value) => value is T t ? this.IndexOf(t) : -1;

    /// <inheritdoc />
    void IList.Insert(int index, object? value) => throw new NotSupportedException();

    /// <inheritdoc />
    void IList.Remove(object? value) => throw new NotSupportedException();

    /// <inheritdoc />
    void IList.RemoveAt(int index) => throw new NotSupportedException();

    /// <inheritdoc />
    void ICollection.CopyTo(Array array, int index)
    {
        foreach (var item in this)
        {
            array.SetValue(item, index++);
        }
    }
}
