// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls;

/// <summary>
///     Custom flyout surface that renders menu data using <see cref="MenuColumnPresenter"/> columns.
///     This implementation keeps interaction logic reusable across menu containers via
///     <see cref="MenuInteractionController"/>.
/// </summary>
public sealed class MenuFlyout : FlyoutBase
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuFlyout),
        new PropertyMetadata(null));

    /// <summary>
    ///     Identifies the <see cref="MaxColumnHeight"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaxColumnHeightProperty = DependencyProperty.Register(
        nameof(MaxColumnHeight),
        typeof(double),
        typeof(MenuFlyout),
        new PropertyMetadata(480d));

    private MenuFlyoutPresenter? presenter;
    private MenuInteractionController? controller;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuFlyout"/> class.
    /// </summary>
    public MenuFlyout()
    {
        this.Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft;
        this.Opening += this.OnFlyoutOpening;
        this.Opened += this.OnFlyoutOpened;
        this.Closing += this.OnFlyoutClosing;
    }

    /// <summary>
    ///     Occurs when a leaf menu item is invoked within the flyout.
    /// </summary>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    ///     Gets or sets the menu source consumed by the flyout.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets the maximum height applied to each visible column.
    /// </summary>
    public double MaxColumnHeight
    {
        get => (double)this.GetValue(MaxColumnHeightProperty);
        set => this.SetValue(MaxColumnHeightProperty, value);
    }

    /// <summary>
    ///     Gets or sets the navigation mode supplied by the owning surface.
    /// </summary>
    public MenuNavigationMode OwnerNavigationMode { get; set; } = MenuNavigationMode.PointerInput;

    /// <inheritdoc />
    protected override Control CreatePresenter()
    {
        this.presenter = new MenuFlyoutPresenter();
        return this.presenter;
    }

    private void OnFlyoutOpening(object? sender, object e)
    {
        if (this.MenuSource is null)
        {
            return;
        }

        this.presenter?.Reset();
        this.controller = new MenuInteractionController(this.MenuSource.Services);
        this.controller.ItemInvoked += this.OnControllerItemInvoked;
        this.controller.SubmenuRequested += this.OnControllerSubmenuRequested;

        switch (this.OwnerNavigationMode)
        {
            case MenuNavigationMode.KeyboardInput:
                this.controller.NotifyKeyboardNavigation();
                break;
            default:
                this.controller.NotifyPointerNavigation();
                break;
        }

        this.presenter?.Initialize(this.MenuSource, this.controller, this.MaxColumnHeight);
    }

    private void OnFlyoutOpened(object? sender, object e)
    {
        this.presenter?.FocusFirstItemAsync();
    }

    private void OnFlyoutClosing(object? sender, FlyoutBaseClosingEventArgs e)
    {
        this.presenter?.Reset();

        if (this.controller is null)
        {
            return;
        }

        this.controller.ItemInvoked -= this.OnControllerItemInvoked;
        this.controller.SubmenuRequested -= this.OnControllerSubmenuRequested;
        this.controller = null;
    }

    private void OnControllerSubmenuRequested(object? sender, MenuSubmenuRequestEventArgs e) => this.presenter?.ShowSubmenu(e);

    private void OnControllerItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        this.ItemInvoked?.Invoke(this, e);
        this.Hide();
    }
}
