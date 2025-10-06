// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Presenter used by <see cref="MenuFlyout"/> to render one or more cascading menu columns.
/// </summary>
[TemplatePart(Name = ContentScrollViewerPart, Type = typeof(ScrollViewer))]
public sealed class MenuFlyoutPresenter : FlyoutPresenter, IMenuInteractionSurface
{
    private const string ContentScrollViewerPart = "ContentScrollViewer";

    private readonly List<MenuColumnPresenter> columnPresenters = new();
    private readonly StackPanel columnsHost;
    private MenuInteractionController? controller;
    private IMenuSource? menuSource;
    private IMenuInteractionSurface? rootSurface;
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
        Debug.WriteLine("[MenuFlyoutPresenter] ctor initialized columns host");
    }

    /// <summary>
    ///     Gets or sets the owning <see cref="MenuFlyout"/> hosting this presenter.
    /// </summary>
    internal MenuFlyout? Owner { get; set; }

    /// <summary>
    ///     Gets the root interaction surface coordinating the menu bar for this presenter.
    /// </summary>
    internal IMenuInteractionSurface? RootSurface => this.rootSurface;

    /// <inheritdoc />
    public void FocusRoot(MenuItemData root, MenuNavigationMode navigationMode)
    {
        _ = root;
        _ = navigationMode;
        throw new NotSupportedException("Column presenters cannot focus root items.");
    }

    /// <inheritdoc />
    public void OpenRootSubmenu(MenuItemData root, FrameworkElement origin, MenuNavigationMode navigationMode)
    {
        _ = root;
        _ = origin;
        _ = navigationMode;
        throw new NotSupportedException("Column presenters cannot open root submenus.");
    }

    /// <inheritdoc />
    public void CloseFromColumn(int columnLevel)
    {
        Debug.WriteLine($"[MenuFlyoutPresenter] CloseFromColumn requested at level {columnLevel}");
        if (columnLevel < 0)
        {
            columnLevel = 0;
        }

        for (var i = this.columnPresenters.Count - 1; i >= columnLevel; i--)
        {
            Debug.WriteLine($"[MenuFlyoutPresenter] Removing column level {i}");
            this.columnsHost.Children.RemoveAt(i);
            this.columnPresenters.RemoveAt(i);
        }

        if (columnLevel == 0 && this.menuSource is not null && this.controller is not null)
        {
            Debug.WriteLine("[MenuFlyoutPresenter] Rebuilding root column after close");
            var rootItems = this.menuSource.Items.ToList();
            _ = this.AddColumn(rootItems, 0, this.controller.NavigationMode);
        }
    }

    /// <inheritdoc />
    public void FocusColumnItem(MenuItemData item, int columnLevel, MenuNavigationMode navigationMode)
    {
        Debug.WriteLine($"[MenuFlyoutPresenter] FocusColumnItem level={columnLevel}, item={item.Id}, mode={navigationMode}");
        var presenter = this.GetColumn(columnLevel);
        presenter?.FocusItem(item, navigationMode);
    }

    /// <inheritdoc />
    public void OpenChildColumn(MenuItemData parent, FrameworkElement origin, int columnLevel, MenuNavigationMode navigationMode)
    {
        _ = origin;
        _ = navigationMode;

        if (this.controller is null || this.menuSource is null)
        {
            Debug.WriteLine("[MenuFlyoutPresenter] OpenChildColumn aborted: controller or menuSource null");
            return;
        }

        this.TrimColumns(columnLevel);

        if (!parent.SubItems.Any())
        {
            Debug.WriteLine($"[MenuFlyoutPresenter] Parent {parent.Id} has no children, skipping column open");
            return;
        }

        var nextLevel = columnLevel + 1;
        var itemsView = new MenuSourceView(parent.SubItems, this.menuSource.Services);
        var items = itemsView.Items.ToList();
        Debug.WriteLine($"[MenuFlyoutPresenter] Opening child column level {nextLevel} for parent {parent.Id} with {items.Count} items (mode={navigationMode})");
        _ = this.AddColumn(items, nextLevel, navigationMode);

        var activationSource = navigationMode == MenuNavigationMode.KeyboardInput
            ? MenuInteractionInputSource.KeyboardInput
            : MenuInteractionInputSource.PointerInput;

        this.RequestFocusForChildColumn(items, nextLevel, activationSource, parent.Id);
    }

    /// <inheritdoc />
    public void Invoke(MenuItemData item, MenuInteractionInputSource inputSource)
        => this.Owner?.RaiseItemInvoked(new MenuItemInvokedEventArgs
        {
            InputSource = inputSource,
            ItemData = item,
        });

    /// <inheritdoc />
    public void NavigateRoot(MenuInteractionHorizontalDirection direction, MenuNavigationMode navigationMode)
    {
        _ = direction;
        _ = navigationMode;
        throw new NotSupportedException("MenuFlyoutPresenter does not coordinate root-level navigation.");
    }

    /// <inheritdoc />
    public void ReturnFocusToApp()
    {
        // The flyout host relinquishes focus automatically when it closes, so no additional
        // action is required here.
    }

    /// <summary>
    ///     Primes the presenter with the supplied data source and controller before display.
    /// </summary>
    /// <param name="menuSource">The menu source providing root items.</param>
    /// <param name="controller">The shared interaction controller.</param>
    /// <param name="columnHeight">The maximum height applied to each column.</param>
    /// <param name="rootSurface">Optional root interaction surface coordinating a parent menu bar.</param>
    internal void Initialize(IMenuSource menuSource, MenuInteractionController controller, double columnHeight, IMenuInteractionSurface? rootSurface)
    {
        this.menuSource = menuSource ?? throw new ArgumentNullException(nameof(menuSource));
        this.controller = controller ?? throw new ArgumentNullException(nameof(controller));
        this.rootSurface = rootSurface;
        this.maxColumnHeight = columnHeight;
        var rootSurfaceName = rootSurface?.GetType().Name ?? "<null>";
        Debug.WriteLine($"[MenuFlyoutPresenter] Initialize called (columnHeight={columnHeight}, rootSurface={rootSurfaceName})");
        this.ResetColumns();
    }

    /// <summary>
    ///     Attempts to focus the first menu item once the presenter is displayed.
    /// </summary>
    internal void FocusFirstItem()
    {
        if (this.menuSource is null || this.controller is null)
        {
            return;
        }

        var firstItem = this.menuSource.Items.FirstOrDefault();
        if (firstItem is null)
        {
            Debug.WriteLine("[MenuFlyoutPresenter] FocusFirstItem aborted: no root items");
            return;
        }

        var navigationMode = this.Owner?.OwnerNavigationMode ?? this.controller.NavigationMode;

        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            if (this.controller is null)
            {
                Debug.WriteLine("[MenuFlyoutPresenter] FocusFirstItem aborted: controller lost before dispatch");
                return;
            }

            var context = MenuInteractionContext.ForColumn(0, this, this.rootSurface);
            var activationSource = this.controller.NavigationMode == MenuNavigationMode.KeyboardInput
                ? MenuInteractionInputSource.KeyboardInput
                : MenuInteractionInputSource.PointerInput;

            Debug.WriteLine($"[MenuFlyoutPresenter] Requesting initial focus for root item {firstItem.Id} (source={activationSource})");
            this.controller.OnFocusRequested(context, origin: null, firstItem, activationSource, openSubmenu: false);
        });
    }

    /// <summary>
    ///     Clears any realized columns and detaches controller subscriptions.
    /// </summary>
    internal void Reset()
    {
        Debug.WriteLine("[MenuFlyoutPresenter] Reset invoked");
        this.ClearColumns();
        this.menuSource = null;
        this.controller = null;
        this.rootSurface = null;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.ResetColumns();
    }

    private void ResetColumns()
    {
        this.ClearColumns();

        if (this.menuSource is null || this.controller is null)
        {
            Debug.WriteLine("[MenuFlyoutPresenter] ResetColumns aborted: missing menuSource or controller");
            return;
        }

        Debug.WriteLine("[MenuFlyoutPresenter] Building root column during reset");
        var rootItems = this.menuSource.Items.ToList();
        _ = this.AddColumn(rootItems, 0, this.controller.NavigationMode);
    }

    private void ClearColumns()
    {
        this.columnsHost.Children.Clear();
        this.columnPresenters.Clear();
    }

    private MenuColumnPresenter AddColumn(List<MenuItemData> items, int level, MenuNavigationMode navigationMode)
    {
        if (this.controller is null)
        {
            throw new InvalidOperationException("Controller must be set before adding columns.");
        }

        var effectiveHeight = double.IsNaN(this.maxColumnHeight) ? 480d : this.maxColumnHeight;

        var column = new MenuColumnPresenter
        {
            Controller = this.controller,
            ItemsSource = items,
            ColumnLevel = level,
            MaxHeight = effectiveHeight,
            OwnerPresenter = this,
        };

        column.Margin = level == 0 ? new Thickness(0) : new Thickness(4, 0, 0, 0);
        this.columnPresenters.Add(column);
        this.columnsHost.Children.Add(column);
        Debug.WriteLine($"[MenuFlyoutPresenter] Added column level {level} with {items.Count} items (mode={navigationMode})");
        column.FocusFirstItem(navigationMode);
        return column;
    }

    private void RequestFocusForChildColumn(List<MenuItemData> items, int columnLevel, MenuInteractionInputSource source, string parentId)
    {
        if (this.rootSurface is null)
        {
            Debug.WriteLine($"[MenuFlyoutPresenter] Cannot focus child column {columnLevel}: rootSurface is null (parent={parentId})");
            return;
        }

        MenuItemData? firstItem = null;
        MenuItemData? firstNonSeparator = null;
        MenuItemData? target = null;

        foreach (var item in items)
        {
            firstItem ??= item;

            if (item.IsSeparator)
            {
                continue;
            }

            firstNonSeparator ??= item;

            if (item.IsEnabled)
            {
                target = item;
                break;
            }
        }

        target ??= firstNonSeparator ?? firstItem;

        if (target is null)
        {
            Debug.WriteLine($"[MenuFlyoutPresenter] No focusable child items for parent {parentId} at column {columnLevel}");
            return;
        }

        var context = MenuInteractionContext.ForColumn(columnLevel, this, this.rootSurface);
        Debug.WriteLine($"[MenuFlyoutPresenter] Scheduling focus for child item {target.Id} at column {columnLevel} (source={source})");

        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            var currentController = this.controller;
            if (currentController is null)
            {
                Debug.WriteLine("[MenuFlyoutPresenter] Controller lost before child focus dispatch");
                return;
            }

            if (!ReferenceEquals(this.controller, currentController))
            {
                Debug.WriteLine($"[MenuFlyoutPresenter] Child focus dispatch skipped: controller changed before focus for {target.Id}");
                return;
            }

            currentController.OnFocusRequested(context, origin: null, target, source, openSubmenu: false);
        });
    }

    private void TrimColumns(int columnLevel)
    {
        for (var i = this.columnPresenters.Count - 1; i > columnLevel; i--)
        {
            this.columnsHost.Children.RemoveAt(i);
            this.columnPresenters.RemoveAt(i);
        }
    }

    private MenuColumnPresenter? GetColumn(int columnLevel) =>
        this.columnPresenters.FirstOrDefault(presenter => presenter.ColumnLevel == columnLevel);
}
