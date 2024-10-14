// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

[SuppressMessage(
    "ReSharper",
    "VirtualMemberNeverOverridden.Global",
    Justification = "methods need to be overridable for testing and extension of the class")]
public abstract class MultipleSelectionModel<T> : SelectionModel<T>
{
    private readonly SelectionObservableCollection<T> selectedIndices;

    /// <summary>
    /// Initializes a new instance of the <see cref="MultipleSelectionModel{T}" /> class.
    /// </summary>
    protected MultipleSelectionModel()
    {
        this.selectedIndices
            = new SelectionObservableCollection<T>(new HashSet<int>())
            {
                GetItemAt = this.GetItemAt,
            };
        this.SelectedIndices = new ReadOnlyObservableCollection<int>(this.selectedIndices);
    }

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
    public ReadOnlyObservableCollection<T> SelectedItems
    {
        // We expect that the majority of use cases will rather use the SelectedIndices collection. Therefore we accept
        // that access to the SelectedItems collection is done rarely and it will be good enough to create the collection
        // on the fly when it is needed.
        get
        {
            var selectedItems = this.selectedIndices.Select(this.GetItemAt);
            return new ReadOnlyObservableCollection<T>(new ObservableCollection<T>(selectedItems));
        }
    }

    /// <inheritdoc />
    public override void ClearAndSelectItemAt(int index)
    {
        this.ValidIndexOrThrow(index);

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
    /// Triggers change notifications for the <see cref="SelectionModel{T}.SelectedIndex" /> and <see cref="SelectionModel{T}.SelectedItem" /> properties if their values change, and the <see cref="SelectedIndices" />
    /// observable collection if its content changes.
    /// </remarks>
    public override void ClearSelection()
    {
        this.selectedIndices.Clear();
        this.SetSelectedIndex(-1);
    }

    /// <summary>
    /// Clear the selection of the item at the given index. If the given index is not selected or not in the valid range, nothing
    /// will happen.
    /// </summary>
    /// <param name="index">
    /// The selected item to deselect.
    /// </param>
    /// <remarks>
    /// If the <see cref="SelectionModel{T}.SelectedIndex" /> is the same than the <see paramref="index" /> to be cleared, its value
    /// is updated to the last item in the <see cref="SelectedIndices" /> collection if it's not empty or <c>-1</c> otherwise.
    /// <para>
    /// Triggers change notifications for the <see cref="SelectionModel{T}.SelectedIndex" /> and <see cref="SelectionModel{T}.SelectedItem" />
    /// properties if their values change, and the <see cref="SelectedIndices" /> observable collection if its content changes.
    /// </para>
    /// </remarks>
    public override void ClearSelection(int index)
    {
        var removed = this.selectedIndices.Remove(index);
        if (!removed)
        {
            return;
        }

        var newSelectedIndex = this.selectedIndices.Count == 0 ? -1 : this.selectedIndices[^1];
        this.SetSelectedIndex(newSelectedIndex);
    }

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

        if (this.IsSelected(index))
        {
            return;
        }

        this.selectedIndices.Add(index);
        this.SetSelectedIndex(index);
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
    /// The first valid index given will become the selected index / selected item.
    /// </para>
    /// </summary>
    /// <param name="indices">
    /// One or more index values that will be added to the selection if they are within the valid range, and do not duplicate
    /// indices already in the existing selection. If no values are provided, the current selection is cleared.
    /// </param>
    public virtual void SelectItemsAt(params int[] indices)
    {
        // Bailout quickly if the underlying data model has no items.
        var itemsCount = this.GetItemCount();
        if (itemsCount == 0)
        {
            return;
        }

        // If no values are provided, the current selection is cleared.
        if (indices.Length == 0)
        {
            this.ClearSelection();
            return;
        }

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            this.ClearSelection();

            _ = indices
                .Where(index => index >= 0 && index < itemsCount)
                .Select(
                    index =>
                    {
                        this.selectedIndices.Add(index);
                        return index;
                    })
                .DefaultIfEmpty(-1)
                .Last();

            if (this.selectedIndices.Count != 0)
            {
                this.SetSelectedIndex(this.selectedIndices[0]);
            }
        }
    }

    /// <summary>
    /// Selects all indices from the given <paramref name="startIndex" /> index to the item before the given <paramref name="endIndex" />
    /// index. This means that the selection is inclusive of the <paramref name="startIndex" /> index, and inclusive of the
    /// <paramref name="endIndex" /> index.
    /// <para>
    /// This method will work regardless of whether start &lt; end or start &gt; end: the only constant is that the given
    /// <paramref name="endIndex" /> index will become the <see cref="SelectionModel{T}.SelectedIndex">SelectedIndex</see>.
    /// </para>
    /// </summary>
    /// <param name="startIndex">
    /// The first index to select - this index will be selected.
    /// </param>
    /// <param name="endIndex">
    /// The last index of the selection - this index will be selected.
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If the given <paramref name="startIndex" /> or <paramref name="endIndex" /> index is less than zero, or greater than or equal to the
    /// total number of items in the underlying data model).
    /// </exception>
    public virtual void SelectRange(int startIndex, int endIndex)
    {
        this.ValidIndexOrThrow(startIndex);
        this.ValidIndexOrThrow(endIndex);

        var ascending = startIndex < endIndex;
        var low = ascending ? startIndex : endIndex;
        var high = ascending ? endIndex : startIndex;

        var arrayLength = high - low + 1;
        var indices = new int[arrayLength];

        var startValue = ascending ? low : high;
        for (var index = 0; index < arrayLength; index++)
        {
            indices[index] = ascending ? startValue++ : startValue--;
        }

        this.SelectItemsAt(indices);
    }

    /// <summary>
    /// Selects a range of items between the specified start and end items, inclusive.
    /// </summary>
    /// <param name="start">The first item in the range.</param>
    /// <param name="end">The last item in the range.</param>
    /// <exception cref="ArgumentException">
    /// Thrown when either <paramref name="start" /> or <paramref name="end" /> are not found in the model items collection.
    /// </exception>
    public void SelectRange(T start, T end)
    {
        var startIndex = this.IndexOf(start);
        if (startIndex == -1)
        {
            throw new ArgumentException("item not found", nameof(start));
        }

        var endIndex = this.IndexOf(end);
        if (endIndex == -1)
        {
            throw new ArgumentException("item not found", nameof(end));
        }

        this.SelectRange(startIndex, endIndex);
    }

    /// <summary>
    /// Convenience method to select all available indices.
    /// </summary>
    public void SelectAll()
    {
        // Bailout quickly if the underlying data model has no items.
        var itemsCount = this.GetItemCount();
        if (itemsCount == 0)
        {
            return;
        }

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            this.ClearSelection();
            for (var index = 0; index < itemsCount; index++)
            {
                this.selectedIndices.Add(index);
            }
        }

        // TODO: Manage focus
        this.SetSelectedIndex(this.selectedIndices[^1]);
    }

    /// <summary>
    /// Inverts the selection state of all items in the collection.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the underlying data model does not implement <see cref="ISelectable" />.
    /// </exception>
    public void InvertSelection()
    {
        if (!typeof(ISelectable).IsAssignableFrom(typeof(T)))
        {
            throw new InvalidOperationException(
                "Cannot invert selection when the underlying data model does not implement ISelectable.");
        }

        // Bailout quickly if the underlying data model has no items.
        var itemsCount = this.GetItemCount();
        if (itemsCount == 0)
        {
            return;
        }

        // Modify the collection quietly. We'll only trigger a collection change
        // notification after we resume notifications.
        using (this.selectedIndices.SuspendNotifications())
        {
            for (var index = 0; index < itemsCount; index++)
            {
                var item = this.GetItemAt(index) as ISelectable;
                Debug.Assert(item is not null, "InvertSelection throws if the type T does not implement ISelectable");
                if (item.IsSelected)
                {
                    this.selectedIndices.Remove(index);
                }
                else
                {
                    this.selectedIndices.Add(index);
                }
            }
        }

        // TODO: Manage focus
        this.SetSelectedIndex(this.selectedIndices.Count == 0 ? -1 : this.selectedIndices[^1]);
    }

    public override string ToString()
    {
        var count = this.selectedIndices.Count;
        return this.SelectedIndex == -1
            ? "No selection"
            : $"{count} selected item{(count == 1 ? string.Empty : "s")} [{string.Join(',', this.selectedIndices)}]";
    }

    /// <summary>
    /// Validates the specified index and throws an <see cref="ArgumentOutOfRangeException" /> if the index is out of range.
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
}
