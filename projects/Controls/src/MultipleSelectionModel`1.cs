// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.Diagnostics;

internal abstract class MultipleSelectionModel<T> : SelectionModel<T>
{
    private readonly SelectionObservableCollection<int> selectedIndices = [];
    private SelectionMode selectionMode = SelectionMode.Single;

    /// <summary>
    /// Initializes a new instance of the <see cref="MultipleSelectionModel{T}" /> class.
    /// </summary>
    protected MultipleSelectionModel()
        => this.SelectedIndices = new ReadOnlyObservableCollection<int>(this.selectedIndices);

    /// <summary>
    /// Gets a <see cref="ReadOnlyObservableCollection{T}" /> of all selected indices. The collection will be updated further by
    /// the selection model to always reflect changes in selection. This can be observed by overriding the
    /// <see cref="ReadOnlyObservableCollection{T}.OnCollectionChanged" /> method on the returned collection.
    /// </summary>
    public ReadOnlyObservableCollection<int> SelectedIndices { get; }

    /// <summary>
    /// Gets a <see cref="ReadOnlyObservableCollection{T}" /> of all selected items. The collection will be updated further by
    /// the selection model to always reflect changes in selection. This can be observed by overriding the
    /// <see cref="ReadOnlyObservableCollection{T}.OnCollectionChanged" /> method on the returned collection.
    /// </summary>
    public ReadOnlyObservableCollection<T> SelectedItems => throw new NotImplementedException();

    /// <summary>
    /// Gets or sets the <see cref="Controls.SelectionMode">SelectionMode</see> to use in this selection model. The selection mode
    /// specifies how many items in the underlying data model can be selected at any one time.
    /// <para>
    /// By default, the selection mode is <see cref="SelectionMode.Single" />.
    /// </para>
    /// </summary>
    /// <exception cref="ArgumentException">
    /// If the value provided to set the property is not <see cref="SelectionMode.Single" /> or <see cref="SelectionMode.Multiple" />.
    /// </exception>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Style",
        "IDE0010:Add missing cases",
        Justification = "missing case is part of default")]
    public SelectionMode SelectionMode
    {
        get => this.selectionMode;
        set
        {
            if (this.selectionMode == value)
            {
                return;
            }

            switch (value)
            {
                case SelectionMode.Multiple:
                    this.selectionMode = value;
                    break;

                case SelectionMode.Single:
                    this.selectionMode = value;
                    if (!this.IsEmpty())
                    {
                        // We're switching from 'Multiple' to 'Single' selection. Only keep the latest selected item.
                        var lastIndex = this.SelectedIndex;
                        this.ClearSelection();
                        this.SelectItemAt(lastIndex);
                    }

                    break;

                default:
                    throw new ArgumentException(
                        $"{nameof(MultipleSelectionModel<T>)} only supports `{nameof(SelectionMode.Single)}` or `{nameof(SelectionMode.Single)}` selection modes",
                        nameof(value));
            }
        }
    }

    /// <inheritdoc />
    public override void ClearAndSelect(int index)
    {
        this.ValidIndexOrThrow(index);

        // If this method is called while there is only one selected index and
        // it is the same as the given index, it should have no effect.
        if (this.IsSelected(index) && this.selectedIndices.Count == 1)
        {
            Debug.Assert(
                this.SelectedItem is not null,
                "expecting a non-null SelectedItem when we have a selected index");

            // Double check that the selected item is the same than the item corresponding to the given index.
            if (this.SelectedItem.Equals(this.GetItemAt(index)))
            {
                return;
            }
        }

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            this.ClearSelection();
            this.SelectItemAt(index);
        }
    }

    /// <summary>
    /// Clears the selection model of any existing selection.
    /// </summary>
    /// <remarks>
    /// Triggers change notifications for the <see cref="SelectionModel{T}.SelectedIndex"/> and <see
    /// cref="SelectionModel{T}.SelectedItem"/> properties if their values change, and the <see cref="SelectedIndices"/>
    /// observable collection if its content changes.
    /// </remarks>
    public override void ClearSelection()
    {
        this.selectedIndices.Clear();
        this.UpdateSelectedIndex(-1);
    }

    /// <summary>
    /// Clear the selection of the item at the given index. If the given index is not selected or not in the valid range, nothing
    /// will happen.
    /// </summary>
    /// <param name="index">
    /// The selected item to deselect.
    /// </param>
    /// <remarks>
    /// Triggers change notifications for the <see cref="SelectionModel{T}.SelectedIndex"/> and <see
    /// cref="SelectionModel{T}.SelectedItem"/> properties if their values change, and the <see cref="SelectedIndices"/>
    /// observable collection if its content changes.
    /// </remarks>
    public override void ClearSelection(int index)
    {
        this.ValidIndexOrThrow(index);

        var removed = this.selectedIndices.Remove(index);
        if (removed)
        {
            var newSelectedIndex = this.IsEmpty() ? -1 : this.selectedIndices[0];
            this.UpdateSelectedIndex(newSelectedIndex);
        }
    }

    /// <inheritdoc />
    public override bool IsEmpty() => this.selectedIndices.Count == 0;

    /// <inheritdoc />
    public override bool IsSelected(int index) => this.selectedIndices.Contains(index);

    /// <inheritdoc />
    public override void SelectItem(T item)
    {
        var index = this.IndexOf(item);
        if (index != -1)
        {
            this.SelectItemAt(index);
        }
    }

    /// <inheritdoc />
    public override void SelectItemAt(int index)
    {
        this.ValidIndexOrThrow(index);

        if (!this.IsSelected(index))
        {
            if (this.SelectionMode == SelectionMode.Single)
            {
                // Modify the collection quietly. We'll only trigger a collection change
                // notification after we resume notifications.
                using (this.selectedIndices.SuspendNotifications())
                {
                    this.ClearSelection();
                    this.selectedIndices.Add(index);
                }
            }
            else
            {
                this.selectedIndices.Add(index);
            }
        }

        this.UpdateSelectedIndex(index);
    }

    /// <summary>
    /// Set the selection to one or more indices at the same time.
    /// <para>
    /// This method will ignore any value that is not within the valid range (i.e. greater than or equal to zero, and less than
    /// the total number of items in the underlying data model). Any duplication of indices will be ignored.
    /// </para>
    /// <para>
    /// If there is already one or more indices selected in this model, calling this method will <strong>not</strong> clear these
    /// selections - to do so it is necessary to first call <see cref="ClearSelection()" />.
    /// </para>
    /// <para>
    /// The last valid index given will become the selected index / selected item.
    /// </para>
    /// </summary>
    /// <param name="indices">
    /// One or more index values that will be added to the selection if they are within the valid range, and do not duplicate
    /// indices already in the existing selection.
    /// </param>
    public void SelectItemsAt(params int[] indices)
    {
        if (indices.Length == 0)
        {
            return;
        }

        var rowCount = this.selectedIndices.Count;

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            this.ClearSelection();

            // If selection mode is single, only process the last valid index in the provided indices.
            if (this.SelectionMode == SelectionMode.Single)
            {
                for (var i = indices.Length - 1; i >= 0; i--)
                {
                    var index = indices[i];

                    // Ignore invalid indices
                    if (index < 0 || index >= rowCount)
                    {
                        continue;
                    }

                    this.selectedIndices.Add(index);
                    this.UpdateSelectedIndex(index);
                    break;
                }
            }
            else
            {
                var lastIndex = indices
                    .Where(index => index >= 0 && index < rowCount)
                    .Select(index =>
                    {
                        this.selectedIndices.Add(index);
                        return index;
                    })
                    .DefaultIfEmpty(-1)
                    .Last();

                if (lastIndex != -1)
                {
                    this.UpdateSelectedIndex(lastIndex);
                }
            }
        }
    }

    /// <summary>
    /// Selects all indices from the given <paramref name="start" /> index to the item before the given <paramref name="end" />
    /// index. This means that the selection is inclusive of the <paramref name="start" /> index, and exclusive of the <paramref name="end" /> index.
    /// <para>
    /// This method will work regardless of whether start &lt; end or start &gt; end: the only constant is that the index before
    /// the given <paramref name="end" /> index will become the <see cref="SelectionModel{T}.SelectedIndex">SelectedIndex</see>.
    /// </para>
    /// </summary>
    /// <param name="start">
    /// The first index to select - this index will be selected.
    /// </param>
    /// <param name="end">
    /// The last index of the selection - this index will not be selected.
    /// </param>
    public void SelectRange(int start, int end)
    {
        var ascending = start < end;
        var low = ascending ? start : end;
        var high = ascending ? end : low;

        var arrayLength = high - low;
        var indices = new int[arrayLength];

        var startValue = ascending ? low : high;
        for (var index = 0; index < arrayLength; index++)
        {
            indices[index] = ascending ? startValue++ : startValue--;
        }

        this.SelectItemsAt(indices);
    }

    /// <summary>
    /// Convenience method to select all available indices.
    /// </summary>
    public void SelectAll()
    {
        if (this.SelectionMode == SelectionMode.Single)
        {
            return;
        }

        var rowCount = this.selectedIndices.Count;
        if (rowCount == 0)
        {
            return;
        }

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            this.ClearSelection();
            for (var index = 0; index < rowCount; index++)
            {
                this.selectedIndices.Add(index);
            }
        }

        // TODO: Manage focus
        this.UpdateSelectedIndex(rowCount - 1);
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

    /// <summary>
    /// Validates the specified index and throws an <see cref="ArgumentOutOfRangeException"/> if the index is out of range.
    /// </summary>
    /// <param name="index">The index to validate.</param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when the index is less than 0 or greater than or equal to the item
    /// count.</exception>
    private void ValidIndexOrThrow(int index)
    {
        if (index < 0 || index >= this.GetItemCount())
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
    }

    /// <summary>
    /// Updates the selected index to the specified new index (which could be -1 to clear the current values).
    /// <para>
    /// If the new index is valid and different from the current selected index, the selected item is updated accordingly.
    /// </para>
    /// </summary>
    /// <param name="newIndex">The new index to set as selected. Should be -1 or within the valid range [0, ItemCount).</param>
    private void UpdateSelectedIndex(int newIndex)
    {
        Debug.Assert(
            newIndex >= -1 && newIndex < this.GetItemCount(),
            $"{nameof(newIndex)} should be -1 or in the valid range [0, ItemCount)");

        if (!this.SetSelectedIndex(newIndex))
        {
            return;
        }

        var newItem = newIndex == -1 ? default : this.GetItemAt(this.SelectedIndex);
        _ = this.SetSelectedItem(newItem);
    }
}
