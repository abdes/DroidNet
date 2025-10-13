// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls.
/// </summary>
public partial class ColumnPresenter
{
    /// <summary>
    ///     Occurs when a menu item in this column is invoked (clicked or activated via keyboard).
    /// </summary>
    /// <remarks>
    ///     This event is raised after the menu item's command has been executed (if present).
    ///     It allows parent controls to handle the item selection and perform additional
    ///     actions such as closing menus or updating UI state.
    /// </remarks>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;
}
