// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     A button control that shows a menu when clicked and acts as a root surface for keyboard navigation.
/// </summary>
public sealed partial class MenuButton
{
    /// <inheritdoc />
    public object? FocusElement => this.XamlRoot is { } xamlRoot
        ? Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(xamlRoot)
        : null;

    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true) =>
        this.ButtonItemData; // MenuButton is a single root item, so adjacent navigation returns itself

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem() =>
        this.IsMenuOpen ? this.ButtonItemData : null; // Return the button item if the menu is open

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem() =>
        this.FocusState != Microsoft.UI.Xaml.FocusState.Unfocused // Return the button item if this button has focus
        ? this.ButtonItemData
        : null;

    /// <inheritdoc />
    public bool FocusItem(MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        // Focus the button itself
        var focusState = navigationMode switch
        {
            MenuNavigationMode.KeyboardInput => Microsoft.UI.Xaml.FocusState.Keyboard,
            MenuNavigationMode.PointerInput => Microsoft.UI.Xaml.FocusState.Pointer,
            _ => Microsoft.UI.Xaml.FocusState.Programmatic,
        };

        return this.Focus(focusState);
    }

    /// <inheritdoc />
    public bool FocusFirstItem(MenuNavigationMode navigationMode) =>
        this.FocusItem(this.ButtonItemData, navigationMode); // Since there's only one item (the button), focus it

    /// <inheritdoc />
    public void ExpandItem(MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.Show(navigationMode);

    /// <inheritdoc />
    public void CollapseItem(MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        // Collapsing the button means hiding the menu
        if (this.IsMenuOpen)
        {
            this.menuHost?.Dismiss(MenuDismissKind.Programmatic);
        }
    }

    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
        => this.menuHost?.Dismiss(kind);
}
