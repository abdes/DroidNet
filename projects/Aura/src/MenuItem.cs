// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Aura;

/// <summary>
/// Represents a menu item that can be used in a menu flyout or menu bar.
/// </summary>
/// <remarks>
/// The <see cref="MenuItem"/> class provides properties for defining the text, command, and sub-items of a menu item. It also includes a unique identifier for the menu item.
/// </remarks>
public partial class MenuItem : ObservableObject
{
    [ObservableProperty]
    private bool isSelected;

    /// <summary>
    /// Gets the text of the menu item.
    /// </summary>
    public required string Text { get; init; }

    /// <summary>
    /// Gets the command to be executed when the menu item is selected.
    /// </summary>
    public ICommand? Command { get; init; }

    /// <summary>
    /// Gets the collection of sub-items for this menu item.
    /// </summary>
    public IEnumerable<MenuItem> SubItems { get; init; } = [];

    /// <summary>
    /// Gets a unique identifier for the menu item by replacing any dot (`,`) in the menu item
    /// Text with an `_` and returning the resulting string as upper case.
    /// </summary>
    public string Id => this.Text.Replace('.', '_').ToUpperInvariant();
}
