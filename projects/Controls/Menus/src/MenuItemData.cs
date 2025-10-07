// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents a menu item that can be used in a menu flyout or menu bar.
/// </summary>
/// <remarks>
///     The <see cref="MenuItemData"/> class provides properties for defining the text, command, and sub-items
///     of a menu item. It also includes a unique identifier for the menu item.
/// </remarks>
public partial class MenuItemData : ObservableObject
{
    /// <summary>
    ///     Gets or sets the text of the menu item.
    /// </summary>
    [ObservableProperty]
    public partial string Text { get; set; } = string.Empty;

    /// <summary>
    ///     Gets or sets the command to be executed when the menu item is selected.
    /// </summary>
    [ObservableProperty]
    public partial ICommand? Command { get; set; }

    /// <summary>
    ///     Gets or sets the icon source for the menu item.
    /// </summary>
    [ObservableProperty]
    public partial IconSource? Icon { get; set; }

    /// <summary>
    ///     Gets or sets the mnemonic (keyboard shortcut) for the menu item.
    /// </summary>
    [ObservableProperty]
    public partial char? Mnemonic { get; set; }

    /// <summary>
    ///     Gets or sets the keyboard accelerator text to display for this menu item (e.g., "Ctrl+S").
    /// </summary>
    [ObservableProperty]
    public partial string? AcceleratorText { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether this menu item is enabled.
    /// </summary>
    [ObservableProperty]
    public partial bool IsEnabled { get; set; } = true;

    /// <summary>
    ///     Gets or sets a value indicating whether this menu item is a separator.
    /// </summary>
    [ObservableProperty]
    public partial bool IsSeparator { get; set; }

    /// <summary>
    ///     Gets a value indicating whether this menu item is interactive (not a separator and enabled for interaction).
    /// </summary>
    public bool IsInteractive => !this.IsSeparator && this.IsEnabled;

    /// <summary>
    ///     Gets or sets the collection of sub-items for this menu item.
    /// </summary>
    [ObservableProperty]
    public partial IEnumerable<MenuItemData> SubItems { get; set; } = [];

    /// <summary>
    ///     Gets or sets a value indicating whether this menu item is in a checked (toggled) state.
    ///     This corresponds to the visual checkmark / state indicator. For radio groups only one
    ///     item in the same <see cref="RadioGroupId"/> should have <see langword="true"/> at a time.
    /// </summary>
    [ObservableProperty]
    public partial bool IsChecked { get; set; } = false;

    /// <summary>
    ///     Gets or sets a value indicating whether this menu item is currently expanded (has an open submenu).
    ///     When <see langword="true"/> the control may present an expanded visual state indicating that the submenu is visible.
    /// </summary>
    /// <remarks>
    ///     This property is passive, intended to only reflect the current state of the menu item. Changing its value does not
    ///     automatically open or close the submenu.
    /// </remarks>
    [ObservableProperty]
    public partial bool IsExpanded { get; set; } = false;

    /// <summary>
    ///     Gets or sets a value indicating whether this menu item is checkable (can be toggled on/off).
    ///     When <see langword="true"/>, the framework will display a checkmark when <see cref="IsChecked"/> is <see langword="true"/>.
    /// </summary>
    [ObservableProperty]
    public partial bool IsCheckable { get; set; } = false;

    /// <summary>
    ///     Gets or sets the radio group identifier for this menu item.
    ///     Items with the same non-<see langword="null"/> RadioGroupId behave as radio buttons (only one can be selected at a time).
    ///     When set, <see cref="IsCheckable"/> is automatically considered <see langword="true"/>.
    /// </summary>
    [ObservableProperty]
    public partial string? RadioGroupId { get; set; }

    /// <summary>
    ///     Gets or sets the unique hierarchical identifier for this menu item.
    ///     This is automatically assigned by MenuBuilder using the full path (e.g., "FILE.NEW", "EDIT.COPY").
    /// </summary>
    [ObservableProperty]
    public partial string Id { get; set; } = string.Empty;

    /// <summary>
    ///     Gets a value indicating whether this menu item has child items.
    /// </summary>
    public bool HasChildren => this.SubItems.Any();

    /// <summary>
    ///     Gets a value indicating whether this menu item is a leaf item (has no children).
    /// </summary>
    public bool IsLeafItem => !this.HasChildren;

    /// <summary>
    ///     Gets a value indicating whether this menu item should display a selectable state indicator
    ///     (checkmark). <see langword="true"/> when either <see cref="IsCheckable"/> is <see langword="true"/>
    ///     or <see cref="RadioGroupId"/> is set.
    /// </summary>
    public bool HasSelectionState => this.IsCheckable || !string.IsNullOrEmpty(this.RadioGroupId);
}
