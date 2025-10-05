// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
/// Interaction-surface implementation for <see cref="MenuBar"/>.
/// </summary>
public sealed partial class MenuBar
{
    /// <inheritdoc />
    void IMenuInteractionSurface.FocusRoot(MenuItemData root, MenuNavigationMode navigationMode)
    {
        if (this.MenuSource is null)
        {
            return;
        }

        var index = this.MenuSource.Items.IndexOf(root);
        this.OpenRootIndex = index;
        this.pendingRootItem = null;

        var container = this.ResolveRootContainer(root);
        if (container is not null)
        {
            this.activeRootItem = container;

            if (navigationMode == MenuNavigationMode.KeyboardInput)
            {
                _ = container.DispatcherQueue.TryEnqueue(() => container.Focus(FocusState.Programmatic));
            }
        }
    }

    /// <inheritdoc />
    void IMenuInteractionSurface.OpenRootSubmenu(MenuItemData root, FrameworkElement origin, MenuNavigationMode navigationMode)
        => this.OpenRootSubmenuCore(root, origin, navigationMode);

    /// <inheritdoc />
    void IMenuInteractionSurface.NavigateRoot(MenuInteractionHorizontalDirection direction, MenuNavigationMode navigationMode)
    {
        _ = navigationMode; // Navigation mode is inferred via controller updates.
        var key = direction == MenuInteractionHorizontalDirection.Previous ? VirtualKey.Left : VirtualKey.Right;
        _ = this.HandleRootHorizontalNavigation(key);
    }

    /// <inheritdoc />
    void IMenuInteractionSurface.CloseFromColumn(int columnLevel)
    {
        if (columnLevel <= 0)
        {
            this.CloseActiveFlyout();
        }
    }

    /// <inheritdoc />
    void IMenuInteractionSurface.FocusColumnItem(MenuItemData item, int columnLevel, MenuNavigationMode navigationMode)
        => throw new NotSupportedException("MenuBar handles only root-level focus operations.");

    /// <inheritdoc />
    void IMenuInteractionSurface.OpenChildColumn(MenuItemData parent, FrameworkElement origin, int columnLevel, MenuNavigationMode navigationMode)
        => throw new NotSupportedException("MenuBar handles only root-level submenu operations.");

    /// <inheritdoc />
    void IMenuInteractionSurface.Invoke(MenuItemData item)
        => this.ItemInvoked?.Invoke(this, new MenuItemInvokedEventArgs { ItemData = item });

    /// <inheritdoc />
    void IMenuInteractionSurface.ReturnFocusToApp()
        => this.DispatcherQueue.TryEnqueue(() => this.Focus(FocusState.Programmatic));
}
