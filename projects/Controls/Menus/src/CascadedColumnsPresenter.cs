// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presenter used by cascading menu hosts to render one or more menu levels.
/// </summary>
[TemplatePart(Name = ContentScrollViewerPart, Type = typeof(ScrollViewer))]
public sealed partial class CascadedColumnsPresenter : ContentControl
{
    private const string ContentScrollViewerPart = "ContentScrollViewer";
    private readonly List<ColumnPresenter> columnPresenters = [];
    private readonly StackPanel columnsHost;
    private (int columnLevel, int position, MenuNavigationMode navigationMode)? deferredFocusRequest;

    /// <summary>
    ///     Initializes a new instance of the <see cref="CascadedColumnsPresenter"/> class.
    /// </summary>
    public CascadedColumnsPresenter()
    {
        this.DefaultStyleKey = typeof(CascadedColumnsPresenter);
        this.columnsHost = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 4,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        this.Content = this.columnsHost;

        this.LogCreated();
    }

    /// <summary>
    ///     Clears any realized columns and detaches controller subscriptions.
    /// </summary>
    internal void Reset()
    {
        this.LogReset();

        // Clear expansion state on any previously expanded items in the current columns.
        foreach (var expanded in this.columnPresenters
            .Select(p => p.ItemsSource)
            .OfType<IEnumerable<MenuItemData>>()
            .Select(items => items.FirstOrDefault(i => i.IsExpanded))
            .Where(i => i is not null))
        {
            expanded!.IsExpanded = false;
        }

        foreach (var column in this.columnPresenters)
        {
            column.ItemInvoked -= this.Column_ItemInvoked;
        }

        // Clear columns and any deferred focus requests.
        this.columnsHost.Children.Clear();
        this.columnPresenters.Clear();
        this.deferredFocusRequest = null;

        if (this.MenuSource is { Services.InteractionController: { } controller, Items: { } items })
        {
            var rootItems = items.ToList();
            _ = this.AddColumn(rootItems, 0, controller.NavigationMode);
        }
    }

    private ColumnPresenter AddColumn(List<MenuItemData> items, int level, MenuNavigationMode navigationMode)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            this.LogAddColumnControllerNotSet();
            throw new InvalidOperationException("Controller must be set before adding columns.");
        }

        var effectiveHeight = double.IsNaN(this.MaxColumnHeight) ? 480d : this.MaxColumnHeight;

        var column = new ColumnPresenter
        {
            MenuSource = this.MenuSource,
            Controller = controller,
            ItemsSource = items,
            ColumnLevel = level,
            MaxHeight = effectiveHeight,
            OwnerPresenter = this,
            Margin = level == 0 ? new Thickness(0) : new Thickness(4, 0, 0, 0),
        };

        column.ItemInvoked += this.Column_ItemInvoked;

        this.columnPresenters.Add(column);
        this.columnsHost.Children.Add(column);
        this.LogAddedColumn(level, items.Count, navigationMode);

        if (this.deferredFocusRequest is { } request && request.columnLevel == level)
        {
            this.LogProcessingDeferredFocusRequest(level);
            this.LogProcessingDeferredFocusRequestDetailed(level, request.position, request.navigationMode);
            this.deferredFocusRequest = null;
            var focusResult = column.FocusItemAt(request.position, request.navigationMode);
            this.LogDeferredFocusAttemptResult(focusResult, request.position);
        }

        return column;
    }

    private void Column_ItemInvoked(object? sender, MenuItemInvokedEventArgs e)
        => this.ItemInvoked?.Invoke(this, e);

    private ColumnPresenter? GetColumn(MenuLevel level) =>
        this.columnPresenters.FirstOrDefault(presenter => presenter.ColumnLevel == level);
}
