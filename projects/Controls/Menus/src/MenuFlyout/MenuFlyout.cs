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

    /// <summary>
    ///     Gets or sets the interaction controller coordinating menu state.
    /// </summary>
    public MenuInteractionController? Controller { get; set; }

    /// <summary>
    ///     Gets or sets the root interaction surface coordinating the menu bar associated with this flyout.
    /// </summary>
    internal IMenuInteractionSurface? RootSurface { get; set; }

    /// <summary>
    ///     Gets the active column interaction surface rendered by the flyout, when available.
    /// </summary>
    internal IMenuInteractionSurface? ColumnSurface => this.presenter;

    /// <summary>
    ///     Gets or sets a value indicating whether controller dismissal should be suppressed when closing.
    /// </summary>
    internal bool SuppressControllerDismissal { get; set; }

    /// <summary>
    ///     Raises <see cref="ItemInvoked"/> with the supplied menu item data.
    /// </summary>
    /// <param name="item">The item that was invoked.</param>
    internal void RaiseItemInvoked(MenuItemData item)
    {
        this.ItemInvoked?.Invoke(this, new MenuItemInvokedEventArgs { MenuItem = item });
    }

    /// <inheritdoc />
    protected override Control CreatePresenter()
    {
        this.presenter = new MenuFlyoutPresenter
        {
            Owner = this,
        };
        return this.presenter;
    }

    private void OnFlyoutOpening(object? sender, object e)
    {
        if (this.MenuSource is null)
        {
            return;
        }

        this.presenter?.Reset();
        this.SuppressControllerDismissal = false;

        this.controller = this.Controller ?? this.MenuSource.Services.InteractionController;
        if (this.controller is null)
        {
            return;
        }

        var rootSurface = this.RootSurface;

        var source = this.OwnerNavigationMode == MenuNavigationMode.KeyboardInput
            ? MenuInteractionActivationSource.KeyboardInput
            : MenuInteractionActivationSource.PointerInput;

        this.controller.OnNavigationSourceChanged(source);
        this.presenter?.Initialize(this.MenuSource, this.controller, this.MaxColumnHeight, rootSurface);
    }

    private void OnFlyoutOpened(object? sender, object e)
    {
        this.presenter?.FocusFirstItem();
    }

    private void OnFlyoutClosing(object? sender, FlyoutBaseClosingEventArgs e)
    {
        this.presenter?.Reset();

        if (this.controller is null)
        {
            return;
        }

        var shouldDismiss = !this.SuppressControllerDismissal;
        this.SuppressControllerDismissal = false;

        if (shouldDismiss)
        {
            // TODO: Differentiate dismiss kind based on FlyoutBase close reason when available.
            if (this.presenter is not null && this.RootSurface is not null)
            {
                var context = MenuInteractionContext.ForColumn(0, this.presenter, this.RootSurface);
                this.controller.OnDismissRequested(context, MenuDismissKind.Programmatic);
            }
        }

        this.controller = null;
    }
}
