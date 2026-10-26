// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     A control to display a tree as a list of expandable items.
/// </summary>
/// <remarks>
///     The control is split by concern: this file holds the template, view model attachment and the realized rows;
///     the keyboard, focus, pointer, drag-and-drop and type-ahead behaviors live in their own partial files.
/// </remarks>
[TemplatePart(Name = TreeItemPart, Type = typeof(DynamicTreeItem))]
[TemplatePart(Name = ItemsRepeaterPart, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = ScrollViewPart, Type = typeof(ScrollView))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTree : Control
{
    /// <summary>
    /// The name of the root grid part in the DynamicTree control template.
    /// </summary>
    public const string RootGridPart = "PartRootGrid";

    /// <summary>
    /// The name of the ItemsRepeater part in the DynamicTree control template.
    /// </summary>
    public const string ItemsRepeaterPart = "PartItemsRepeater";

    /// <summary>
    /// The name of the ScrollView part in the DynamicTree control template, which hosts the ItemsRepeater. It must be a
    /// <see cref="ScrollView"/>: the repeater virtualizes from the effective viewport its scroll host reports, which
    /// the legacy ScrollViewer leaves stale during animated view changes, realizing no rows.
    /// </summary>
    public const string ScrollViewPart = "PartScrollView";

    /// <summary>
    /// The name of an instantiated tree item part within the DynamicTree item template.
    /// Use to locate the nested <see cref="DynamicTreeItem" /> within the ItemsRepeater.
    /// </summary>
    public const string TreeItemPart = "PartTreeItem";

    /// <summary>The data format a drag started by the tree carries, so drop targets can recognize it.</summary>
    public const string DragDataFormat = "application/vnd.droidnet.dynamictree";

    // The realized rows by the repeater container that hosts them, maintained as the repeater prepares and clears
    // elements, so updates reach only the rows that exist.
    private readonly Dictionary<FrameworkElement, DynamicTreeItem> realizedRows = [];

    private ILogger logger = NullLogger<DynamicTree>.Instance;

    private ItemsRepeater? itemsRepeater;
    private ScrollView? scrollView;
    private Grid? rootGrid;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DynamicTree" /> class.
    /// </summary>
    public DynamicTree()
    {
        this.DefaultStyleKey = typeof(DynamicTree);

        this.IsTabStop = true;

        // Followed for the control's whole life, so a host that clears the view model also clears the rows of an
        // unloaded tree.
        this.ViewModelChanged += this.OnViewModelChanged;
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    /// <summary>Raised when a realized tree row receives a pointer or keyboard context request.</summary>
    /// <remarks>The control does not assign domain meaning or display a menu automatically.</remarks>
    public event EventHandler<TreeItemContextRequestedEventArgs>? ItemContextRequested;

    /// <summary>Raised when a drag of tree items starts, after the tree has added its own data.</summary>
    /// <remarks>Owners add formats that identify the items to drop targets outside the tree.</remarks>
    public event EventHandler<TreeItemsDragStartingEventArgs>? ItemsDragStarting;

    /// <summary>Settles a realized inline rename before another context action changes selection.</summary>
    /// <returns>Whether all realized rename drafts committed successfully.</returns>
    public async Task<bool> SettleRenameAsync()
    {
        foreach (var row in this.GetRealizedRows())
        {
            if (!await row.SettleRenameForContextAsync().ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    ///     Begins in-place rename for the provided item, if it is currently shown by the tree.
    /// </summary>
    /// <param name="item">The item to rename.</param>
    /// <returns><see langword="true"/> if rename UI was opened; otherwise <see langword="false"/>.</returns>
    public async Task<bool> BeginRenameAsync(ITreeItem item)
    {
        if (item is null || this.itemsRepeater is null || this.ViewModel is null)
        {
            return false;
        }

        var index = this.itemsRepeater.ItemsSourceView?.IndexOf(item) ?? this.ViewModel.ShownIndexOf(item);
        if (index == -1)
        {
            return false;
        }

        var element = this.itemsRepeater.TryGetElement(index) ?? this.itemsRepeater.GetOrCreateElement(index);
        if (element is null)
        {
            return false;
        }

        // The rename popup anchors to the row's label when it opens, so the row must already be in its final,
        // visible position: scroll without animation and let layout settle first. Otherwise a row below the
        // viewport opens its editor at the row's old position, clamped to the edge of the pane.
        // An element the repeater just created has no position until it is arranged; bring it into view only then.
        element.UpdateLayout();
        await this.BringIntoViewAsync(element).ConfigureAwait(true);
        await this.WaitForLayoutAsync().ConfigureAwait(true);

        // Ensure the element has focus before starting rename.
        await this.FocusRealizedItemAsync(item, FocusState.Programmatic).ConfigureAwait(true);

        element = this.itemsRepeater.TryGetElement(index) ?? element;
        var treeItem = this.GetRow(element);
        return treeItem is not null && ReferenceEquals(treeItem.ItemAdapter, item) && treeItem.BeginRename();
    }

    /// <summary>Gets a snapshot of the rows the tree currently realizes.</summary>
    /// <returns>The realized rows, in no particular order.</returns>
    internal IReadOnlyList<DynamicTreeItem> GetRealizedRows() => [.. this.realizedRows.Values];

    /// <summary>Selects an item on behalf of an automation client.</summary>
    /// <param name="item">The item to select.</param>
    /// <param name="toggle">Whether to toggle the item in the current selection; otherwise select only it.</param>
    internal void SelectFromAutomation(ITreeItem item, bool toggle)
        => this.SelectItem(item, isControlDown: toggle, isShiftDown: false, RequestOrigin.Programmatic);

    /// <summary>Dispatches a rename to the current owner after verifying the item remains available.</summary>
    /// <param name="item">The logical item captured when rename began.</param>
    /// <param name="newName">The proposed label.</param>
    /// <returns>The owner's rename outcome.</returns>
    internal async Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        var viewModel = this.ViewModel;
        return viewModel?.IsShown(item) == true
            ? await viewModel.CommitRenameAsync(item, newName).ConfigureAwait(true)
            : TreeItemRenameResult.Rejected("The item is no longer available in this tree.");
    }

    /// <inheritdoc />
    protected override AutomationPeer OnCreateAutomationPeer() => new DynamicTreeAutomationPeer(this);

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.itemsRepeater is not null)
        {
            this.itemsRepeater.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown));
            this.itemsRepeater.ElementPrepared -= this.ItemsRepeater_OnElementPrepared;
            this.itemsRepeater.ElementClearing -= this.ItemsRepeater_OnElementClearing;
        }

        this.rootGrid?.RemoveHandler(PointerPressedEvent, new PointerEventHandler(this.OnPointerPressed));

        if (this.GetTemplateChild(ItemsRepeaterPart) is not ItemsRepeater itemsRepeaterPart)
        {
            throw new InvalidOperationException($"{ItemsRepeaterPart} not found in the control template.");
        }

        // Rows of a replaced template are gone with it.
        this.realizedRows.Clear();
        this.itemsRepeater = itemsRepeaterPart;
        this.rootGrid = this.GetTemplateChild(RootGridPart) as Grid;
        this.scrollView = this.GetTemplateChild(ScrollViewPart) as ScrollView;
        this.ApplyFilterBarTemplate();

        this.itemsRepeater.AddHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown), handledEventsToo: true);

        this.itemsRepeater.ElementPrepared += this.ItemsRepeater_OnElementPrepared;
        this.itemsRepeater.ElementClearing += this.ItemsRepeater_OnElementClearing;

        // Hook events that will check for clicks on empty space inside the ItemsRepeater
        // Use AddHandler to be able to handle the events even if something inside ItemsRepeater is also doing it.
        this.rootGrid?.AddHandler(
            PointerPressedEvent,
            new PointerEventHandler(this.OnPointerPressed),
            handledEventsToo: false);
    }

    /// <summary>
    ///     Runs an asynchronous operation started by a synchronous event handler, logging a failure instead of letting
    ///     it escape the handler and end the process.
    /// </summary>
    /// <param name="operation">The operation to run.</param>
    /// <param name="operationName">The name of the calling handler, for the log.</param>
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "a fire-and-forget UI operation has no caller to receive its failure; it is logged")]
    private async void RunGuarded(Func<Task> operation, [CallerMemberName] string? operationName = null)
    {
        try
        {
            await operation().ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogOperationFailed(operationName ?? "<unknown>", exception);
        }
    }

    // Finds the realized row a repeater container hosts; a container the tree has not prepared yet resolves by name.
    private DynamicTreeItem? GetRow(UIElement? element)
    {
        if (element is not FrameworkElement container)
        {
            return null;
        }

        return this.realizedRows.TryGetValue(container, out var row)
            ? row
            : container.FindName(TreeItemPart) as DynamicTreeItem;
    }

    private void ApplyItemLayout(DynamicTreeItem row)
    {
        row.ItemRowHeight = this.ItemRowHeight;
        row.ItemFontSize = this.ItemFontSize;
        row.ItemIconSize = this.ItemIconSize;
        row.ItemIndentWidth = this.ItemIndentWidth;
        row.ItemIconMargin = this.ItemIconMargin;
    }

    private void UpdateThumbnailTemplateForRealizedItems()
    {
        foreach (var row in this.GetRealizedRows())
        {
            row.OnThumbnailTemplateSelectorChanged();
        }
    }

    private void UpdateTrailingContentForRealizedItems()
    {
        foreach (var row in this.GetRealizedRows())
        {
            row.UpdateTrailingContentFromTree();
        }
    }

    private void UpdateItemLayoutForRealizedItems()
    {
        foreach (var row in this.GetRealizedRows())
        {
            this.ApplyItemLayout(row);
        }
    }

    private void OnLoaded(object? sender, RoutedEventArgs args)
    {
        this.itemsRepeater?.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown));
        this.itemsRepeater?.AddHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown), handledEventsToo: true);

        // Attach to the view model now that the tree is live.
        this.OnViewModelChanged(this, new ViewModelChangedEventArgs<DynamicTreeViewModel>(oldValue: null));
    }

    private void OnUnloaded(object? sender, RoutedEventArgs args)
    {
        // A control moved within the tree may be loaded again before this late Unloaded arrives.
        if (this.IsLoaded)
        {
            return;
        }

        this.itemsRepeater?.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown));

        // The repeater keeps its rows: a closing window raises Unloaded while its XAML is torn down, so nothing here
        // changes a XAML property. The view model's interaction state is released for the next tree.
        this.ViewModel?.PropertyChanged -= this.ViewModel_OnPropertyChanged;
        this.ViewModel?.BringIntoViewRequested -= this.ViewModel_OnBringIntoViewRequested;
        this.ViewModel?.DetachTree(this);

        this.ClearDragState();
        this.ResetTypeAhead();
        this.ResetFocusState();
    }

    private void ItemsRepeater_OnElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        if (args.Element is not FrameworkElement element)
        {
            return;
        }

        // Log element clearing
        this.LogElementClearing(element);

        element.PointerPressed -= this.TreeItem_PointerPressed;
        element.PointerReleased -= this.TreeItem_PointerReleased;
        element.GotFocus -= this.TreeItem_GotFocus;
        element.Tapped -= this.TreeItem_Tapped;
        element.DragOver -= this.TreeItem_DragOver;
        element.DragLeave -= this.TreeItem_DragLeave;
        element.Drop -= this.TreeItem_Drop;
        element.ContextRequested -= this.TreeItem_ContextRequested;

        if (!this.realizedRows.Remove(element, out var treeItem))
        {
            return;
        }

        treeItem.DragStarting -= this.TreeItem_DragStarting;
        treeItem.Expand -= this.OnExpandTreeItem;
        treeItem.Collapse -= this.OnCollapseTreeItem;
        treeItem.DoubleTapped -= this.TreeItem_DoubleTapped;

        // The control keeps its own reference to the element carrying the drop cue. A rebuild
        // during a drag (hover-expand changes the collection) recycles that element, and without
        // this the cue stays painted on whichever row the container is next bound to.
        if (ReferenceEquals(this.dropIndicatorElement, element) || ReferenceEquals(this.dropIndicatorElement, treeItem))
        {
            this.ClearDropIndicatorVisual();
        }

        // The row stops following its item and the tree until the repeater prepares it again.
        treeItem.Detach();
    }

    private void ItemsRepeater_OnElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not FrameworkElement element)
        {
            return;
        }

        // Log element prepared
        this.LogElementPrepared(element);

        if (element.FindName(TreeItemPart) is not DynamicTreeItem treeItemPart)
        {
            return;
        }

        this.realizedRows[element] = treeItemPart;

        // The tree is the single source of the row's layout, trailing content and thumbnail selector: it pushes them
        // here and whenever its own properties change.
        this.ApplyItemLayout(treeItemPart);

        // The ViewModel.LoggerFactory never changes after construction, so we set it once
        // when the element gets prepared.
        treeItemPart.LoggerFactory = this.ViewModel?.LoggerFactory;
        treeItemPart.AttachTo(this);

        element.PointerPressed += this.TreeItem_PointerPressed;
        element.PointerReleased += this.TreeItem_PointerReleased;
        element.GotFocus += this.TreeItem_GotFocus;
        element.Tapped += this.TreeItem_Tapped;

        treeItemPart.Collapse += this.OnCollapseTreeItem;
        treeItemPart.Expand += this.OnExpandTreeItem;
        treeItemPart.DoubleTapped += this.TreeItem_DoubleTapped;

        // The row is the drag source. Its DropCompleted stays subscribed while the row exists: a drag that rebuilds
        // the list (hover-expand) recycles the source row before the drag ends, and the tree must still learn that
        // the drag ended.
        treeItemPart.DragStarting += this.TreeItem_DragStarting;
        treeItemPart.DropCompleted -= this.TreeItem_DropCompleted;
        treeItemPart.DropCompleted += this.TreeItem_DropCompleted;
        element.DragOver += this.TreeItem_DragOver;
        element.DragLeave += this.TreeItem_DragLeave;
        element.Drop += this.TreeItem_Drop;
        element.ContextRequested += this.TreeItem_ContextRequested;
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<DynamicTreeViewModel> args)
    {
        // Detach any handlers from the previous ViewModel
        args.OldValue?.PropertyChanged -= this.ViewModel_OnPropertyChanged;
        args.OldValue?.BringIntoViewRequested -= this.ViewModel_OnBringIntoViewRequested;
        args.OldValue?.DetachTree(this);

        // A view model has one live tree; a tree it rejects shows no rows and follows none of its requests. An
        // unloaded tree attaches when it loads.
        if (this.IsLoaded && this.ViewModel is not null && this.ViewModel.TryAttachTree(this))
        {
            this.logger = this.ViewModel.LoggerFactory?.CreateLogger<DynamicTree>() ?? NullLogger<DynamicTree>.Instance;

            this.ViewModel.PropertyChanged -= this.ViewModel_OnPropertyChanged;
            this.ViewModel.PropertyChanged += this.ViewModel_OnPropertyChanged;
            this.ViewModel.BringIntoViewRequested -= this.ViewModel_OnBringIntoViewRequested;
            this.ViewModel.BringIntoViewRequested += this.ViewModel_OnBringIntoViewRequested;
        }

        this.UpdateDisplayedItems();
    }

    private void UpdateDisplayedItems()
    {
        // A loaded tree the view model rejected shows nothing. An unloaded tree keeps showing its own view model's
        // rows, so loading it again rebuilds nothing.
        var isAttached = this.ViewModel?.IsAttachedTree(this) == true;
        if (this.ViewModel is null || (this.IsLoaded && !isAttached))
        {
            this.DisplayedItems = null;
            return;
        }

        var hasPredicate = this.ViewModel.FilterPredicate is not null;
        var useFiltered = this.IsFilteringEnabled && hasPredicate;

        this.LogDisplayedItemsSource(hasPredicate, useFiltered);

        var displayedItems = useFiltered
            ? this.ViewModel.FilteredItems
            : this.ViewModel.ShownItems;
        this.DisplayedItems = displayedItems;
        if (!isAttached)
        {
            return;
        }

        // Keyboard navigation, typeahead and range extension follow the same scope the pointer
        // paths already honour, so one control switch describes every interaction surface.
        this.ViewModel.DisplayedInteractionItems = displayedItems;
        this.ViewModel.InteractionScope = this.SelectionScope;
    }

    private void OnExpandTreeItem(object? sender, DynamicTreeEventArgs args)
    {
        if (this.ViewModel is { } viewModel)
        {
            this.RunGuarded(() => viewModel.ExpandItemAsync(args.TreeItem));
        }
    }

    private void OnCollapseTreeItem(object? sender, DynamicTreeEventArgs args)
    {
        if (this.ViewModel is { } viewModel)
        {
            this.RunGuarded(() => viewModel.CollapseItemAsync(args.TreeItem));
        }
    }

    [SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code readability")]
    [SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "code readability")]
    private List<ITreeItem> GetSelectedItems()
    {
        if (this.ViewModel is null)
        {
            return [];
        }

        return this.ViewModel.ShownItems
            .Where(item => item.IsSelected)
            .ToList();
    }

    private IReadOnlyList<ITreeItem> GetDisplayedItemsSnapshot()
    {
        if (this.itemsRepeater?.ItemsSourceView is { } source)
        {
            var items = new List<ITreeItem>(source.Count);
            for (var index = 0; index < source.Count; index++)
            {
                if (source.GetAt(index) is ITreeItem item)
                {
                    items.Add(item);
                }
            }

            return items;
        }

        return this.ViewModel?.ShownItems.ToArray() ?? [];
    }

    private void SelectItem(
        ITreeItem item,
        bool isControlDown,
        bool isShiftDown,
        RequestOrigin origin = RequestOrigin.PointerInput)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        if (this.SelectionScope == TreeSelectionScope.DisplayedItems)
        {
            this.ViewModel.SelectDisplayedItem(item, this.GetDisplayedItemsSnapshot(), isControlDown, isShiftDown);
            return;
        }

        this.ViewModel.SelectItemCommand.Execute(new(item, origin, isControlDown, isShiftDown));
    }
}
