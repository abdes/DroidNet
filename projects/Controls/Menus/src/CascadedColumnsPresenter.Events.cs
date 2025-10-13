// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presenter used by cascading menu hosts to render one or more menu levels.
/// </summary>
public partial class CascadedColumnsPresenter
{
    /// <summary>
    ///     Occurs when a menu item in a child column is invoked (clicked or activated via keyboard).
    /// </summary>
    /// <remarks>
    ///     This event is raised after the menu item's command has been executed (if present).
    ///     It allows parent controls to handle the item selection and perform additional
    ///     actions such as closing menus or updating UI state.
    /// </remarks>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;
}
