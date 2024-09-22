// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System;
using System.Diagnostics;

/// <summary>
/// A SelectionModel which enforces the rule that only a single index be selected at any given time.
/// </summary>
/// <inheritdoc />
public abstract class SingleSelectionModel<T> : SelectionModel<T>
{
    public override void ClearSelection() => this.UpdateSelectedIndex(-1);

    public override void ClearSelection(int index)
    {
        if (this.SelectedIndex == index)
        {
            this.ClearSelection();
        }
    }

    public override void ClearAndSelect(int index) => this.SelectItemAt(index);

    public override bool IsEmpty() => this.GetItemCount() == 0 || this.SelectedIndex == -1;

    public override bool IsSelected(int index) => this.SelectedIndex == index;

    public override void SelectItem(T item)
    {
        var index = this.IndexOf(item);
        if (index != -1)
        {
            this.UpdateSelectedIndex(index);
        }
    }

    public override void SelectItemAt(int index)
    {
        if (index < 0 || index >= this.GetItemCount())
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }

        this.UpdateSelectedIndex(index);
    }

    /// <summary>
    /// Gets the data model item associated with a specific index.
    /// </summary>
    /// <param name="index">
    /// The position of the item in the underlying data model.
    /// </param>
    /// <returns>
    /// The item that exists at the given index.
    /// </returns>
    protected abstract T GetItemAt(int index);

    /// <summary>
    /// Searches for the specified <paramref name="item" /> in the underlying data model, and returns the zero-based index of its
    /// first occurrence.
    /// </summary>
    /// <param name="item">
    /// The item to locate in the underlying data model.
    /// </param>
    /// <returns>
    /// The zero-based index of the first occurrence of item within the underlying data model, if found; otherwise, -1.
    /// </returns>
    protected abstract int IndexOf(T item);

    /// <summary>
    /// Gets the number of items available for the selection model. If the number of items can change dynamically, it is the
    /// responsibility of the concrete implementation to ensure that items are selected or unselected as appropriate as the items
    /// change.
    /// </summary>
    /// <returns>
    /// A number greater than or equal to 0 representing the number of items available for the selection model.
    /// </returns>
    protected abstract int GetItemCount();

    private void UpdateSelectedIndex(int newIndex)
    {
        Debug.Assert(
            newIndex >= -1 && newIndex < this.GetItemCount(),
            $"{nameof(newIndex)} should be in the valid range [-1, ItemCount)");

        if (!this.SetSelectedIndex(newIndex))
        {
            return;
        }

        var newItem = this.SelectedIndex == -1 ? default : this.GetItemAt(this.SelectedIndex);
        _ = this.SetSelectedItem(newItem);
    }
}
