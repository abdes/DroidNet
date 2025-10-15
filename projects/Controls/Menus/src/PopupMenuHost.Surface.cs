// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     <see cref="ICascadedMenuHost"/> implementation backed by <see cref="Popup"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class PopupMenuHost : ICascadedMenuSurface
{
    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
        => this.presenter.GetAdjacentItem(level, itemData, direction, wrap);

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem(MenuLevel level)
        => this.presenter.GetExpandedItem(level);

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem(MenuLevel level)
        => this.presenter.GetFocusedItem(level);

    /// <inheritdoc />
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.FocusItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode)
        => this.presenter.FocusFirstItem(level, navigationMode);

    /// <inheritdoc />
    public bool ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.ExpandItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.CollapseItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void TrimTo(MenuLevel level)
        => this.presenter.TrimTo(level);

    /// <inheritdoc />
    public bool FocusColumn(MenuLevel level, MenuNavigationMode navigationMode)
        => this.presenter.FocusColumn(level, navigationMode);
}
