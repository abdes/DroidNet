// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/>.
/// </summary>
public sealed partial class MenuBar : IRootMenuSurface
{
    /// <inheritdoc />
    public object? FocusElement => this.XamlRoot is { } xamlRoot
        ? FocusManager.GetFocusedElement(xamlRoot)
        : null;

    /// <inheritdoc />
    public bool Show(MenuNavigationMode navigationMode)
    {
        // Menubar is always visible; for keyboard, set an initial focus anchor.
        if (navigationMode == MenuNavigationMode.KeyboardInput)
        {
            // Focus failure is not critical here; we just tried our best.
            _ = this.FocusFirstItem(navigationMode);
        }

        return true;
    }

    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        this.LogDismiss();
        var expandedItem = this.GetExpandedItem();
        if (expandedItem is not null && this.activeHost is { IsOpen: true } host)
        {
            expandedItem.IsExpanded = false;
            this.LogDismissingHost();
            host.Dismiss(kind);
            return;
        }

        if (this.MenuSource is { Services.InteractionController: { } controller })
        {
            var context = MenuInteractionContext.ForRoot(this, this.activeHost?.Surface);
            controller.OnDismissed(context);
        }

        this.RaiseDismissed(kind);
    }

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem()
    {
        var result = this.MenuSource?.Items.FirstOrDefault(static item => item.IsExpanded);
        this.LogGetExpandedItem(result?.Id);
        return result;
    }

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem()
    {
        if (this.MenuSource is null || this.MenuSource.Items.Count == 0)
        {
            return null;
        }

        if (FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem { ItemData: MenuItemData data }
            && this.MenuSource.Items.Contains(data))
        {
            this.LogGetFocusedItem(data.Id);
            return data;
        }

        this.LogGetFocusedItem(itemId: null);
        return null;
    }

    /// <inheritdoc />
    /// <exception cref="InvalidOperationException">
    ///     Thrown when <see cref="MenuSource"/> is not set.
    /// </exception>
    public MenuItemData GetAdjacentItem(MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
    {
        this.LogGetAdjacentItem(itemData.Id, direction, wrap);

        var items = this.MenuSource?.Items
            ?? throw new InvalidOperationException("MenuSource is not set.");

        var index = items.IndexOf(itemData);
        if (index < 0)
        {
            throw new ArgumentException("The provided item is not part of the menu source.", nameof(itemData));
        }

        var step = direction switch
        {
            MenuNavigationDirection.Left or MenuNavigationDirection.Up => -1,
            MenuNavigationDirection.Right or MenuNavigationDirection.Down => 1,
            _ => 0,
        };

        var count = items.Count;
        var nextIndex = wrap
            ? (index + step + count) % count
            : Math.Clamp(index + step, 0, count - 1);

        return items[nextIndex];
    }

    /// <inheritdoc />
    /// <exception cref="InvalidOperationException">
    ///     Thrown when <see cref="MenuSource"/> is not set.
    /// </exception>
    public bool FocusItem(MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        this.EnsureItemIsRoot(itemData);
        if (this.ResolveToItem(itemData) is not { } menuItem)
        {
            return false;
        }

        if (FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem fe && ReferenceEquals(fe, menuItem))
        {
            return true; // Already focused
        }

        if (!itemData.IsInteractive)
        {
            return false; // Cannot focus non-interactive items
        }

        var focusState = navigationMode switch
        {
            MenuNavigationMode.KeyboardInput => FocusState.Keyboard,
            MenuNavigationMode.PointerInput => FocusState.Pointer,
            MenuNavigationMode.Programmatic => FocusState.Programmatic,
            _ => FocusState.Programmatic,
        };

        return this.DispatcherQueue.TryEnqueue(() =>
        {
            var result = menuItem.Focus(focusState);
            this.LogFocusItem(itemData.Id, navigationMode, result);
        });
    }

    /// <inheritdoc />
    public bool FocusFirstItem(MenuNavigationMode navigationMode)
    {
        foreach (var item in this.Items)
        {
            if (item.IsInteractive)
            {
                return this.FocusItem(item, navigationMode);
            }
        }

        return false;
    }

    /// <inheritdoc />
    public bool ExpandItem(MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        Debug.Assert(this.MenuSource is { }, "OpenRootSubmenu requires a valid MenuSource");
        this.EnsureItemIsRoot(itemData);

        if (!itemData.HasChildren)
        {
            return true;
        }

        var currentExpanded = this.GetExpandedItem();
        if (ReferenceEquals(currentExpanded, itemData))
        {
            return true; // Already expanded
        }

        this.LogExpandItem(itemData.Id, navigationMode);

        _ = currentExpanded?.IsExpanded = false;

        var host = this.EnsureHost();
        this.LogSettingMenuSourceOnHost();
        host.MenuSource = new MenuSourceView(itemData.SubItems, this.MenuSource.Services);
        var item = this.ResolveToItem(itemData);
        Debug.Assert(item is not null, "Expecting itemData to resolve to a valid MenuItem.");

        this.LogShowingHost();

        return this.DispatcherQueue.TryEnqueue(() => host.ShowAt(item, navigationMode));
    }

    /// <inheritdoc />
    public void CollapseItem(MenuItemData itemData, MenuNavigationMode navigationMode)
    {
        this.EnsureItemIsRoot(itemData);
        if (!itemData.IsExpanded)
        {
            return;
        }

        this.LogCollapseItem(itemData.Id, navigationMode);
        itemData.IsExpanded = false;
    }

    private void EnsureItemIsRoot(MenuItemData item)
    {
        if (!this.Items.Contains(item))
        {
            throw new ArgumentException("The provided item is not part of the menu source.", nameof(item));
        }
    }
}
