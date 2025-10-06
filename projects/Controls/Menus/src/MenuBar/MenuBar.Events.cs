// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through the custom <see cref="MenuFlyout"/> presenter.
/// </summary>
public sealed partial class MenuBar
{
    /// <summary>
    ///     Occurs when a leaf menu item is invoked through the bar.
    /// </summary>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;
}
