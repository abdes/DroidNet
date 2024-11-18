// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Selection;

/// <summary>
/// A <see cref="SelectionModel{T}"/> which enforces the rule that only a single index can be selected at any given time.
/// </summary>
/// <typeparam name="T">
/// The type of the item that can be selected, which is typically the type of items in the control.
/// </typeparam>
/// <remarks>
/// <para>
/// The <see cref="SingleSelectionModel{T}"/> class provides a base implementation for managing
/// single selection in controls such as lists or grids.
/// It ensures that only one item can be selected at a time.
/// </para>
/// <para>
/// This class raises property change notifications for the <see cref="SelectionModel{T}.SelectedIndex"/>,
/// <see cref="SelectionModel{T}.SelectedItem"/>, and <see cref="SelectionModel{T}.IsEmpty"/> properties,
/// allowing the UI to update automatically when the selection changes.
/// </para>
/// </remarks>
public abstract class SingleSelectionModel<T> : SelectionModel<T>
{
    /// <inheritdoc/>
    public override void ClearSelection() => this.SetSelectedIndex(-1);

    /// <summary>
    /// Clear the current selection if the given <paramref name="index" /> is the selected one.
    /// If the given index is not selected or not in the valid range, nothing will happen.
    /// </summary>
    /// <param name="index">
    /// The selected item to deselect.
    /// </param>
    /// <remarks>
    /// Triggers change notifications for the <see cref="SelectionModel{T}.SelectedIndex" /> and
    /// <see cref="SelectionModel{T}.SelectedItem" />
    /// properties if their values change.
    /// </remarks>
    public override void ClearSelection(int index)
    {
        if (this.SelectedIndex == index)
        {
            this.ClearSelection();
        }
    }

    /// <inheritdoc/>
    public override void ClearAndSelectItemAt(int index) => this.SelectItemAt(index);

    /// <inheritdoc/>
    public override bool IsSelected(int index) => this.SelectedIndex == index;

    /// <inheritdoc/>
    public override void SelectItem(T item)
    {
        var index = this.IndexOf(item);
        if (index != -1)
        {
            _ = this.SetSelectedIndex(index);
        }
    }

    /// <inheritdoc/>
    public override void SelectItemAt(int index)
    {
        if (index < 0 || index >= this.GetItemCount())
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }

        _ = this.SetSelectedIndex(index);
    }

    /// <inheritdoc/>
    public override string ToString()
        => this.SelectedIndex == -1 ? "No selection" : $"1 selected item ({this.SelectedItem})";
}
