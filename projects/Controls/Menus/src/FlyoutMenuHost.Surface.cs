// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     A <see cref="ICascadedMenuHost"/> implementation backed by <see cref="FlyoutBase"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class FlyoutMenuHost
{
    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
        => this.RequirePresenter().GetAdjacentItem(level, itemData, direction, wrap);

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem(MenuLevel level)
        => this.RequirePresenter().GetExpandedItem(level);

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem(MenuLevel level)
        => this.RequirePresenter().GetFocusedItem(level);

    /// <inheritdoc />
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().FocusItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode)
        => this.RequirePresenter().FocusFirstItem(level, navigationMode);

    /// <inheritdoc />
    public void ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().ExpandItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().CollapseItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void TrimTo(MenuLevel level)
        => this.RequirePresenter().TrimTo(level);
}
