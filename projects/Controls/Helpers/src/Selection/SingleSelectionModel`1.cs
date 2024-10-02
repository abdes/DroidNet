// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System;

/// <summary>
/// A SelectionModel which enforces the rule that only a single index be selected at any given time.
/// </summary>
/// <inheritdoc />
public abstract class SingleSelectionModel<T> : SelectionModel<T>
{
    public override void ClearSelection() => this.SetSelectedIndex(-1);

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
            this.SetSelectedIndex(index);
        }
    }

    public override void SelectItemAt(int index)
    {
        if (index < 0 || index >= this.GetItemCount())
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }

        this.SetSelectedIndex(index);
    }
}
