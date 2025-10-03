// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Presenter used by <see cref="MenuFlyout"/> to render one or more cascading menu columns.
/// </summary>
[TemplatePart(Name = ContentScrollViewerPart, Type = typeof(ScrollViewer))]
public sealed class MenuFlyoutPresenter : FlyoutPresenter
{
    private const string ContentScrollViewerPart = "ContentScrollViewer";

    private readonly List<MenuColumnPresenter> columnPresenters = new();
    private readonly StackPanel columnsHost;
    private MenuInteractionController? controller;
    private IMenuSource? menuSource;
    private double maxColumnHeight;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuFlyoutPresenter"/> class.
    /// </summary>
    public MenuFlyoutPresenter()
    {
        this.DefaultStyleKey = typeof(MenuFlyoutPresenter);
        this.columnsHost = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 4,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        this.Content = this.columnsHost;
    }

    /// <summary>
    ///     Primes the presenter with the supplied data source and controller before display.
    /// </summary>
    /// <param name="menuSource">The menu source providing root items.</param>
    /// <param name="controller">The shared interaction controller.</param>
    /// <param name="columnHeight">The maximum height applied to each column.</param>
    internal void Initialize(IMenuSource menuSource, MenuInteractionController controller, double columnHeight)
    {
        this.menuSource = menuSource ?? throw new ArgumentNullException(nameof(menuSource));
        this.controller = controller ?? throw new ArgumentNullException(nameof(controller));
        this.maxColumnHeight = columnHeight;

        this.ResetColumns();
        this.controller.SubmenuRequested -= this.OnControllerSubmenuRequested;
        this.controller.SubmenuRequested += this.OnControllerSubmenuRequested;
    }

    /// <summary>
    ///     Materializes or updates submenu columns based on the supplied request.
    /// </summary>
    /// <param name="request">The submenu request raised by the interaction controller.</param>
    internal void ShowSubmenu(MenuSubmenuRequestEventArgs request)
    {
        if (this.controller is null)
        {
            return;
        }

        this.TrimColumns(request.ColumnLevel);

        if (!request.MenuItem.SubItems.Any())
        {
            return;
        }

        var columnLevel = request.ColumnLevel + 1;
        var items = new MenuSourceView(request.MenuItem.SubItems, this.menuSource!.Services).Items;
        this.AddColumn(items, columnLevel);
    }

    /// <summary>
    ///     Attempts to focus the first menu item asynchronously once the presenter is displayed.
    /// </summary>
    internal void FocusFirstItemAsync()
    {
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            if (this.columnsHost.Children.FirstOrDefault() is MenuColumnPresenter presenter)
            {
                presenter.Focus(FocusState.Programmatic);
            }
        });
    }

    /// <summary>
    ///     Clears any realized columns and detaches controller subscriptions.
    /// </summary>
    internal void Reset()
    {
        if (this.controller is not null)
        {
            this.controller.SubmenuRequested -= this.OnControllerSubmenuRequested;
        }

        this.ResetColumns();
        this.menuSource = null;
        this.controller = null;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.ResetColumns();
    }

    private void ResetColumns()
    {
        this.columnsHost.Children.Clear();
        this.columnPresenters.Clear();

        if (this.menuSource is null || this.controller is null)
        {
            return;
        }

        this.AddColumn(this.menuSource.Items, 0);
    }

    private void AddColumn(IEnumerable<MenuItemData> items, int level)
    {
        if (this.controller is null)
        {
            return;
        }

        var effectiveHeight = double.IsNaN(this.maxColumnHeight) ? 480d : this.maxColumnHeight;

        var column = new MenuColumnPresenter
        {
            Controller = this.controller,
            ItemsSource = items,
            ColumnLevel = level,
            MaxHeight = effectiveHeight,
        };

        column.Margin = level == 0 ? new Thickness(0) : new Thickness(4, 0, 0, 0);
        this.columnPresenters.Add(column);
        this.columnsHost.Children.Add(column);
    }

    private void TrimColumns(int columnLevel)
    {
        for (var i = this.columnPresenters.Count - 1; i > columnLevel; i--)
        {
            this.columnsHost.Children.RemoveAt(i);
            this.columnPresenters.RemoveAt(i);
        }
    }

    private void OnControllerSubmenuRequested(object? sender, MenuSubmenuRequestEventArgs e)
    {
        this.ShowSubmenu(e);
    }
}
