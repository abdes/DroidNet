// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>
/// An abstract class used by controls to provide a consistent interface for maintaining a selection.
/// </summary>
/// <typeparam name="T">
/// The type of the item that can be selected, which is typically the type of items in the control.
/// </typeparam>
public abstract class SelectionModel<T> : ObservableObject
{
    private int selectedIndex = -1;

    private T? selectedItem;

    /// <summary>
    /// Gets the currently selected index value in the selection model. The selected index is either <c>-1</c>, to represent that
    /// there is no selection, or an integer value that is within the range of the underlying data model size.
    /// </summary>
    /// <remarks>
    /// The selected index property is most commonly used when the selection model only allows single selection, but is equally
    /// applicable when in multiple selection mode. When in this mode, the selected index will always represent the last selection
    /// made.
    /// </remarks>
    /// TODO: Make SelectedIndex ObservableProperty
    public int SelectedIndex => this.selectedIndex;

    /// <summary>
    /// Gets the currently selected item in the selection model. The selected item is either <see langword="null" />, to represent
    /// that there is no selection, or an <see cref="object" /> that is retrieved from the underlying data model of the control the
    /// selection model is associated with.
    /// </summary>
    /// TODO: Make SelectedItem ObservableProperty
    public T? SelectedItem => this.selectedItem;

    /// <summary>
    /// Select the given index in the selection model, assuming the index is within the valid range (i.e. greater than or equal to
    /// zero, and less than the total number of items in the underlying data model).
    /// </summary>
    /// <remarks>
    /// If there is already one or more indices selected in this model, calling this method will not clear these selections - to
    /// do so it is necessary to first call ClearSelection().
    /// <para>
    /// If the index is already selected, it will not be selected again, or unselected. However, if multiple selection is
    /// implemented, then calling select on an already selected index will have the effect of making the index the new selected
    /// index (as returned by <see cref="SelectedIndex" />>().
    /// </para>
    /// </remarks>
    /// <param name="index">
    /// The position of the item to select in the selection model.
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If the given <paramref name="index" /> is less than zero, or greater than or equal to the total number of items in the
    /// underlying data model).
    /// </exception>
    public abstract void SelectItemAt(int index);

    /// <summary>
    /// This method will attempt to select the index that contains the given <paramref name="item" />.
    /// </summary>
    /// <remarks>
    /// This method will look for the first occurrence of the given <paramref name="item" />, and if successful, it will select it.
    /// This means that this method will not select multiple occurrences, and will have no effect if the item was not found.
    /// </remarks>
    /// <param name="item">
    /// The item to attempt to select in the underlying data model.
    /// </param>
    public abstract void SelectItem(T item);

    /// <summary>
    /// Clears the selection model of any existing selection.
    /// </summary>
    /// <remarks>
    /// Triggers change notifications for the <see cref="SelectedIndex"/> and <see cref="SelectedItem"/> properties if their values
    /// change.
    /// </remarks>
    public abstract void ClearSelection();

    /// <summary>
    /// Clear the selection of the item at the given index. If the given index is not selected or not in the valid range, nothing
    /// will happen.
    /// </summary>
    /// <param name="index">
    /// The selected item to deselect.
    /// </param>
    /// <remarks>
    /// Triggers change notifications for the <see cref="SelectedIndex"/> and <see cref="SelectedItem"/> properties if their values
    /// change.
    /// </remarks>
    public abstract void ClearSelection(int index);

    /// <summary>
    /// Clears any selection prior to setting the selection to the given index.
    /// </summary>
    /// <remarks>
    /// The purpose of this method is to avoid having to call <see cref="ClearSelection()" /> first, meaning that observers that
    /// are listening to the selected index property will not see the selected index being temporarily set to -1.
    /// </remarks>
    /// <param name="index">
    /// The index that should be the only selected index in this selection model.
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If the given <paramref name="index" /> is less than zero, or greater than or equal to the total number of items in the
    /// underlying data model).
    /// </exception>
    public abstract void ClearAndSelect(int index);

    /// <summary>
    /// Convenience method to inform if the given <paramref name="index" /> is currently selected in this SelectionModel.
    /// </summary>
    /// <param name="index">
    /// The index to check as to whether it is currently selected or not.
    /// </param>
    /// <returns>
    /// <see langword="true" /> if the given <paramref name="index" /> is selected; <see langword="false" /> otherwise.
    /// </returns>
    /// <remarks>
    /// If the given <paramref name="index" /> is not in the valid range, this method will simply return <see langword="false" />.
    /// </remarks>
    public abstract bool IsSelected(int index);

    /// <summary>
    /// Check if there are any selected indices/items.
    /// </summary>
    /// <returns>
    /// <see langword="true" /> if there are no selected items, and <see langword="false" /> if there are.
    /// </returns>
    public abstract bool IsEmpty();

    /// <summary>
    /// Compares the current and new values for the <see cref="SelectedIndex" /> property. If the value has changed, raises the
    /// <see cref="ObservableObject.PropertyChanging" /> event, updates the property and then raises the
    /// <see cref="ObservableObject.PropertyChanged" /> event.
    /// </summary>
    /// <param name="value">
    /// The property's value after the change occurred.
    /// </param>
    /// <returns>
    /// <see langword="true" /> if the property was changed, <see langword="false" /> otherwise.
    /// </returns>
    /// <remarks>
    /// The <see cref="ObservableObject.PropertyChanging" /> and <see cref="ObservableObject.PropertyChanged" /> events are not
    /// raised if the current and new value are the same.
    /// </remarks>
    protected bool SetSelectedIndex(int value)
        => this.SetProperty(ref this.selectedIndex, value, nameof(this.SelectedIndex));

    /// <summary>
    /// Compares the current and new values for the <see cref="SelectedItem" /> property. If the value has changed, raises the
    /// <see cref="ObservableObject.PropertyChanging" /> event, updates the property and then raises the
    /// <see cref="ObservableObject.PropertyChanged" /> event.
    /// </summary>
    /// <param name="value">
    /// The property's value after the change occurred.
    /// </param>
    /// <returns>
    /// <see langword="true" /> if the property was changed, <see langword="false" /> otherwise.
    /// </returns>
    /// <remarks>
    /// The <see cref="ObservableObject.PropertyChanging" /> and <see cref="ObservableObject.PropertyChanged" /> events are not
    /// raised if the current and new value are the same.
    /// </remarks>
    protected bool SetSelectedItem(T? value)
        => this.SetProperty(ref this.selectedItem, value, nameof(this.SelectedItem));
}
