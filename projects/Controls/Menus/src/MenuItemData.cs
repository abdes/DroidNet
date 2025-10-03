// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Represents a menu item that can be used in a menu flyout or menu bar.
/// </summary>
/// <remarks>
/// The <see cref="MenuItemData"/> class provides properties for defining the text, command, and sub-items of a menu item. It also includes a unique identifier for the menu item.
/// </remarks>
public partial class MenuItemData : ObservableObject
{
    /// <summary>
    /// Gets or sets the text of the menu item.
    /// </summary>
    [ObservableProperty]
    public partial string Text { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the command to be executed when the menu item is selected.
    /// </summary>
    [ObservableProperty]
    public partial ICommand? Command { get; set; }

    /// <summary>
    /// Gets or sets the icon source for the menu item.
    /// </summary>
    [ObservableProperty]
    public partial IconSource? Icon { get; set; }

    /// <summary>
    /// Gets or sets the mnemonic (keyboard shortcut) for the menu item.
    /// </summary>
    [ObservableProperty]
    public partial char? Mnemonic { get; set; }

    /// <summary>
    /// Gets or sets the keyboard accelerator text to display for this menu item (e.g., "Ctrl+S").
    /// </summary>
    [ObservableProperty]
    public partial string? AcceleratorText { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether this menu item is enabled.
    /// </summary>
    [ObservableProperty]
    public partial bool IsEnabled { get; set; } = true;

    /// <summary>
    /// Gets or sets a value indicating whether this menu item is a separator.
    /// </summary>
    [ObservableProperty]
    public partial bool IsSeparator { get; set; }

    /// <summary>
    /// Gets or sets the collection of sub-items for this menu item.
    /// </summary>
    [ObservableProperty]
    public partial IEnumerable<MenuItemData> SubItems { get; set; } = [];

    /// <summary>
    /// Gets or sets a value indicating whether this menu item is in a checked (toggled) state.
    /// This corresponds to the visual checkmark / state indicator. For radio groups only one
    /// item in the same <see cref="RadioGroupId"/> should have <c>true</c> at a time.
    /// </summary>
    /// <remarks>
    /// This property replaces the former <c>IsSelected</c> (now deprecated) which previously
    /// drove both the background highlight and the checkmark. The highlight responsibility has
    /// moved to <see cref="IsActive"/> so that persistent toggle state (checked) is visually
    /// independent from transient keyboard navigation focus.
    /// </remarks>
    [ObservableProperty]
    public partial bool IsChecked { get; set; } = false;

    /// <summary>
    /// Gets or sets a value indicating whether this menu item is the currently active (hot tracked)
    /// item due to keyboard navigation. When <c>true</c> the control may present an accent/background
    /// highlight independent from the logical checked state (<see cref="IsChecked"/>).
    /// </summary>
    /// <remarks>
    /// This does not toggle automatically when the item is clicked; it is intended to be managed by
    /// the containing menu system when moving with arrow keys. Mouse hover should continue to use
    /// pointer over visual states and not force <see cref="IsActive"/>.
    /// </remarks>
    [ObservableProperty]
    public partial bool IsActive { get; set; } = false;

    /// <summary>
    /// Gets or sets a value indicating whether this menu item is checkable (can be toggled on/off).
    /// When true, the framework will display a checkmark when <see cref="IsChecked"/> is true.
    /// </summary>
    [ObservableProperty]
    public partial bool IsCheckable { get; set; } = false;

    /// <summary>
    /// Gets or sets the radio group identifier for this menu item.
    /// Items with the same non-null RadioGroupId behave as radio buttons (only one can be selected at a time).
    /// When set, IsCheckable is automatically considered true.
    /// </summary>
    [ObservableProperty]
    public partial string? RadioGroupId { get; set; }

    /// <summary>
    /// Gets or sets the unique hierarchical identifier for this menu item.
    /// This is automatically assigned by MenuBuilder using the full path (e.g., "FILE.NEW", "EDIT.COPY").
    /// </summary>
    [ObservableProperty]
    public partial string Id { get; set; } = string.Empty;

    /// <summary>
    /// Gets a value indicating whether this menu item has child items.
    /// </summary>
    public bool HasChildren => this.SubItems.Any();

    /// <summary>
    /// Gets a value indicating whether this menu item is a leaf item (has no children).
    /// </summary>
    public bool IsLeafItem => !this.HasChildren;

    /// <summary>
    /// Gets a value indicating whether this menu item should display a selectable state indicator
    /// (checkmark). True when either <see cref="IsCheckable"/> is <c>true</c> or <see cref="RadioGroupId"/> is set.
    /// </summary>
    public bool HasSelectionState => this.IsCheckable || !string.IsNullOrEmpty(this.RadioGroupId);
}
