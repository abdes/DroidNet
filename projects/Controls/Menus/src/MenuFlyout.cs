// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Custom flyout surface that renders menu data using <see cref="ColumnPresenter"/> columns.
///     This implementation keeps interaction logic reusable across menu containers via
///     <see cref="MenuInteractionController"/>.
/// </summary>
public sealed partial class MenuFlyout : FlyoutBase
{
    private CascadedColumnsPresenter? presenter;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuFlyout"/> class.
    /// </summary>
    public MenuFlyout()
    {
        this.Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft;
        this.LightDismissOverlayMode = LightDismissOverlayMode.Off;

        this.Opening += this.OnFlyoutOpening;
        this.Closing += this.OnFlyoutClosing;
    }

    /// <summary>
    ///     Gets the active column interaction surface rendered by the flyout, when available.
    /// </summary>
    internal CascadedColumnsPresenter? Presenter => this.presenter;

    private MenuInteractionController? Controller => this.MenuSource?.Services.InteractionController;

    /// <summary>
    ///     Dismisses the flyout by hiding it, after completely resetting the presenter state.
    /// </summary>
    /// <param name="kind">The kind of dismiss action being performed.</param>
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        this.LogDismiss(kind);
        this.presenter?.Dismiss(kind);
        this.Hide();
    }

    /// <inheritdoc />
    protected override Control CreatePresenter()
    {
        this.LogCreatePresenter();
        this.presenter = new CascadedColumnsPresenter();
        return this.presenter;
    }

    private void OnFlyoutOpening(object? sender, object e)
    {
        Debug.Assert(this.presenter is { }, "Presenter should be null");

        this.LogOpening();

        if (this.MenuSource is null)
        {
            this.LogNoMenuSource();
            return;
        }

        // If the Flyout is used with a root surface (e.g., MenuBar), ensure the root surface
        // is set as the pass-through element for input events.
        if (this.RootSurface is UIElement passThrough)
        {
            this.OverlayInputPassThroughElement = passThrough;
        }

        this.presenter.MaxColumnHeight = this.MaxColumnHeight;
        this.presenter.MenuSource = this.MenuSource;
        this.presenter.RootSurface = this.RootSurface;
    }

    private void OnFlyoutClosing(object? sender, FlyoutBaseClosingEventArgs e)
    {
        this.LogClosing();

        // Handle situations where the flyout is open and closed before the presenter is created.
        if (this.presenter is not { })
        {
            return;
        }

        this.OverlayInputPassThroughElement = null;

        this.presenter.Reset();
    }
}
