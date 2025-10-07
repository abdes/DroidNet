// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presenter used by <see cref="MenuFlyout"/> to render one or more cascading menu columns.
/// </summary>
public sealed partial class CascadedColumnsPresenter : ICascadedMenuSurface
{
    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        this.LogDismiss(kind);
        this.MenuSource = null; // This will completely clear the menu state.
    }

    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
    {
        var presenter = this.GetColumn(level) ?? throw new ArgumentException("Invalid column level", nameof(level));
        var result = presenter.GetAdjacentItem(itemData, direction, wrap);
        this.LogGetAdjacentItem(level, itemData.Id, direction, wrap);
        return result;
    }

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem(MenuLevel level)
    {
        var presenter = this.GetColumn(level) ?? throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        var result = presenter.GetExpandedItem();
        this.LogGetExpandedItem(level, result?.Id);
        return result;
    }

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem(MenuLevel level)
    {
        var presenter = this.GetColumn(level) ?? throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        var result = presenter.GetFocusedItem();
        this.LogGetFocusedItem(level, result?.Id);
        return result;
    }

    /// <inheritdoc />
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        var presenter = this.GetColumn(level) ?? throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        var result = presenter.FocusItem(itemData, navigationMode);
        this.LogFocusItem(level, itemData.Id, navigationMode);
        return result;
    }

    /// <inheritdoc />
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode)
    {
        var presenter = this.GetColumn(level) ?? throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        var result = presenter.FocusFirstItem(navigationMode);
        this.LogFocusFirstItem(level, navigationMode);
        return result;
    }

    /// <inheritdoc />
    public void ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        this.LogExpandItem(level, itemData.Id, navigationMode);

        // Ensure the requested level corresponds to an open column
        if (level < 0 || level >= this.columnPresenters.Count)
        {
            this.LogInvalidColumnLevel(level);
            throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        }

        // If the item has no children, there's nothing to expand
        if (!itemData.HasChildren)
        {
            this.LogParentHasNoChildren(itemData.Id);
            return;
        }

        var services = this.MenuSource?.Services;
        Debug.Assert(services is not null, "Expecting to have valid MenuSource and MenuServices when expanding an item.");

        var nextLevel = new MenuLevel(level + 1);
        var itemsView = new MenuSourceView(itemData.SubItems, services);
        var items = itemsView.Items.ToList();

        this.LogOpeningChildColumn(nextLevel, itemData.Id, items.Count, navigationMode);
        _ = this.AddColumn(items, nextLevel, navigationMode);
        itemData.IsExpanded = true;
    }

    /// <inheritdoc />
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        this.LogCollapseItem(level, itemData.Id, navigationMode);
        Debug.Assert(itemData.IsExpanded, "CollapseItem should only be called on an expanded item.");

        // Ensure the requested level corresponds to an open column
        if (level < 0 || level >= this.columnPresenters.Count)
        {
            this.LogInvalidColumnLevel(level);
            throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        }

        this.TrimTo(level);
        itemData.IsExpanded = false;
    }

    /// <inheritdoc />
    public void TrimTo(MenuLevel level)
    {
        if (level < 0 || level >= this.columnPresenters.Count)
        {
            this.LogInvalidColumnLevel(level);

            // Match interface Contract: Throw when level is negative or exceeds number of open columns.
            throw new ArgumentOutOfRangeException(nameof(level), level, "The specified menu level does not correspond to an open column on this surface.");
        }

        var initialCount = this.columnPresenters.Count;
        for (var i = this.columnPresenters.Count - 1; i > level; i--)
        {
            this.columnsHost.Children.RemoveAt(i);
            this.columnPresenters.RemoveAt(i);
        }

        this.LogTrimColumns(level, initialCount);
    }
}
