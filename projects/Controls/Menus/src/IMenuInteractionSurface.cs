// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
/// Represents a host surface capable of responding to interaction commands issued by
/// <see cref="MenuInteractionController"/>. This indirection lets unit tests mock menu
/// surfaces while production controls forward the calls to their existing helpers.
/// </summary>
public interface IMenuInteractionSurface
{
    /// <summary>Focuses the provided root menu item within the surface.</summary>
    /// <param name="root">The root item to focus.</param>
    /// <param name="navigationMode">The navigation mode associated with the focus.</param>
    public void FocusRoot(MenuItemData root, MenuNavigationMode navigationMode);

    /// <summary>Opens the submenu associated with the supplied root item.</summary>
    /// <param name="root">The root item whose submenu should open.</param>
    /// <param name="origin">The visual element representing the root item.</param>
    /// <param name="navigationMode">The navigation mode associated with the request.</param>
    public void OpenRootSubmenu(MenuItemData root, FrameworkElement origin, MenuNavigationMode navigationMode);

    /// <summary>Moves focus between root items hosted by the surface.</summary>
    /// <param name="direction">The horizontal direction to traverse.</param>
    /// <param name="navigationMode">The navigation mode associated with the traversal.</param>
    public void NavigateRoot(MenuInteractionHorizontalDirection direction, MenuNavigationMode navigationMode);

    /// <summary>Closes columns starting at the specified level.</summary>
    /// <param name="columnLevel">The zero-based column level to close from.</param>
    public void CloseFromColumn(int columnLevel);

    /// <summary>Focuses the given menu item within a column.</summary>
    /// <param name="item">The menu item to focus.</param>
    /// <param name="columnLevel">The column level hosting the item.</param>
    /// <param name="navigationMode">The navigation mode associated with the focus.</param>
    public void FocusColumnItem(MenuItemData item, int columnLevel, MenuNavigationMode navigationMode);

    /// <summary>Opens a child column for the supplied parent menu item.</summary>
    /// <param name="parent">The parent item owning the child column.</param>
    /// <param name="origin">The visual element representing the parent item.</param>
    /// <param name="columnLevel">The current column level for the parent item.</param>
    /// <param name="navigationMode">The navigation mode associated with the request.</param>
    public void OpenChildColumn(MenuItemData parent, FrameworkElement origin, int columnLevel, MenuNavigationMode navigationMode);

    /// <summary>Invokes the supplied menu item.</summary>
    /// <param name="item">The item to invoke.</param>
    /// <param name="inputSource">The input source behind this invocation.</param>
    public void Invoke(MenuItemData item, MenuInteractionInputSource inputSource);

    /// <summary>Returns focus to the hosting application shell.</summary>
    public void ReturnFocusToApp();
}
