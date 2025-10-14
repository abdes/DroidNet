// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/>.
/// </summary>
public sealed partial class MenuBar
{
    /// <summary>
    ///     Occurs when the menu bar dismisses its root surface.
    /// </summary>
    public event EventHandler<MenuDismissedEventArgs>? Dismissed;

    private void RaiseDismissed(MenuDismissKind kind)
        => this.Dismissed?.Invoke(this, new MenuDismissedEventArgs(kind));
}
