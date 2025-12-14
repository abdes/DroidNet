// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using static DroidNet.Controls.TreeDisplayHelper;

namespace DroidNet.Controls;

/// <summary>
///     Represents the ViewModel for a dynamic tree control, providing functionality for managing
///     hierarchical data structures, including selection, expansion, and manipulation of tree items.
/// </summary>
/// <param name="loggerFactory">
///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
/// <remarks>
///     This class provides the foundational implementation for a dynamic tree view model. It includes
///     methods for expanding and collapsing tree items, initializing the root item, inserting and
///     removing items, and managing selection within the tree.
///     <para>
///     To create a concrete implementation of this view model, derive from <see cref="DynamicTreeViewModel" />
///     and implement the necessary logic for your specific tree structure. Below is an example of how
///     to derive from this class and create a concrete view model and item adapter.</para>
/// </remarks>
public abstract partial class DynamicTreeViewModel(ILoggerFactory? loggerFactory = null) : ObservableObject, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<DynamicTreeViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<DynamicTreeViewModel>();
    private readonly ObservableCollection<ITreeItem> shownItems = [];

    private TreeDisplayHelper? displayHelper;
    private FocusedItemInfo? focusedItem;

    private bool disposed;

    /// <summary>
    /// Indicates the origin of a focus or action request made against the tree.
    /// </summary>
    public enum RequestOrigin
    {
        /// <summary>
        /// The request originated from a pointer device, such as a mouse or touch.
        /// </summary>
        PointerInput,

        /// <summary>
        /// The request originated from keyboard input (for example arrow keys or Tab).
        /// </summary>
        KeyboardInput,

        /// <summary>
        /// The request was initiated programmatically by code.
        /// </summary>
        Programmatic,
    }

    /// <summary>
    ///     Gets the items currently shown in the tree.
    /// </summary>
    /// <remarks>
    ///     This collection is updated dynamically as items are expanded or collapsed. The returned
    ///     object supports collection-change notifications, but the surface exposed by this property
    ///     is intentionally query-only.
    /// </remarks>
    public IEnumerable<ITreeItem> ShownItems => this.shownItems;

    /// <summary>
    ///     Gets the <see cref="ILoggerFactory"/> used to create loggers for this view model.
    /// </summary>
    internal ILoggerFactory? LoggerFactory { get; } = loggerFactory;

    /// <summary>
    ///     Gets the item that currently holds logical keyboard focus within the tree.
    /// </summary>
    protected internal FocusedItemInfo? FocusedItem
    {
        get => this.focusedItem;
        private set
        {
            // Avoid side effects if focused item and origin are unchanged.
            var old = this.focusedItem;
            if (old is null && value is null)
            {
                return;
            }

            if (old is not null && value is not null
                && ReferenceEquals(old.Item, value.Item)
                && old.Origin == value.Origin)
            {
                return;
            }

            this.focusedItem = value;
            this.OnPropertyChanged(nameof(this.FocusedItem));
        }
    }

    /// <summary>
    ///     Gets the number of items currently shown in the tree.
    /// </summary>
    protected internal int ShownItemsCount => this.shownItems.Count;

    private TreeDisplayHelper DisplayHelper => this.displayHelper ??=
        new TreeDisplayHelper(
            this.shownItems,
            () => this.SelectionModel,
            this.ExpandItemAsync,
            new TreeDisplayEventCallbacks(
                args =>
                {
                    this.ItemBeingAdded?.Invoke(this, args);
                    if (args.Proceed)
                    {
                        this.InvalidateClipboardDueToMutation();
                    }

                    return args.Proceed;
                },
                args =>
                {
                    this.InvalidateClipboardDueToMutation();
                    this.ItemAdded?.Invoke(this, args);
                },
                args =>
                {
                    this.ItemBeingRemoved?.Invoke(this, args);
                    if (args.Proceed)
                    {
                        this.InvalidateClipboardDueToMutation();
                    }

                    return args.Proceed;
                },
                args =>
                {
                    this.InvalidateClipboardDueToMutation();
                    this.ItemRemoved?.Invoke(this, args);
                },
                args =>
                {
                    this.ItemBeingMoved?.Invoke(this, args);
                    if (args.Proceed)
                    {
                        this.InvalidateClipboardDueToMutation();
                    }

                    return args.Proceed;
                },
                args =>
                {
                    this.InvalidateClipboardDueToMutation();
                    this.ItemMoved?.Invoke(this, args);
                }),
            this.LoggerFactory);

    /// <summary>
    ///     Expands the specified tree item (which must be visible in the tree) asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to expand.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the item is not yet visible in the tree and cannot be expanded.</exception>
    public async Task ExpandItemAsync(ITreeItem itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        if (!itemAdapter.IsRoot && itemAdapter.Parent?.IsExpanded != true)
        {
            // The item's parent has never loaded its children, or is collapsed.
            this.LogExpandItemNotVisible(itemAdapter);
            throw new InvalidOperationException("cannot expand item; its parent is not expanded");
        }

        this.LogExpandItem(itemAdapter);
        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = true;
    }

    /// <summary>
    ///     Collapses the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to collapse.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     If the item is already collapsed, this method does nothing.
    /// </remarks>
    public async Task CollapseItemAsync(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        await this.HideChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = false;
    }

    /// <summary>
    ///     Initializes the root item of the tree asynchronously.
    /// </summary>
    /// <param name="root">The root item to initialize.</param>
    /// <param name="skipRoot">
    ///     If <see langword="true" />, the root item itself is not added to the shown items collection,
    ///     only its children are added.
    /// </param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task InitializeRootAsync(ITreeItem root, bool skipRoot = true)
    {
        this.LogInitializeRoot(root, skipRoot);
        this.SelectionModel?.ClearSelection();
        this.LogShownItemsClear();
        this.shownItems.Clear();

        if (!skipRoot)
        {
            this.LogShownItemsAdd(root);
            this.shownItems.Add(root);
        }
        else
        {
            root.IsExpanded = true;
        }

        if (root.IsExpanded)
        {
            await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(false);
        }

        this.SyncSelectionModelWithItems();
        _ = this.EnsureFocus(RequestOrigin.PointerInput);
    }

    /// <summary>
    ///     Inserts a child item at the specified index under the given parent item asynchronously.
    /// </summary>
    /// <param name="relativeIndex">Zero-based index at which to insert under the parent.</param>
    /// <param name="parent">The parent that receives the child.</param>
    /// <param name="item">The child item to insert.</param>
    /// <remarks>
    ///     <paramref name="relativeIndex"/> is expressed in the coordinate system of <paramref name="parent"/>'s
    ///     children collection (<see cref="ITreeItem.Children"/>). It is <em>not</em> an index into
    ///     <see cref="ShownItems"/>, which depends on expansion state.
    ///     <para>
    ///     If <paramref name="parent"/> is not expanded, it will be auto-expanded to perform the insertion.
    ///     Consumers implementing undo/redo should record <see cref="TreeItemAddedEventArgs.Parent"/> and
    ///     <see cref="TreeItemAddedEventArgs.RelativeIndex"/> (child index), not the visual index.</para>
    /// </remarks>
    /// <returns>A task that completes when the insertion finishes.</returns>
    public async Task InsertItemAsync(int relativeIndex, ITreeItem parent, ITreeItem item)
        => await this.DisplayHelper.InsertItemAsync(relativeIndex, parent, item).ConfigureAwait(true);

    /// <summary>
    ///     Removes the specified item and its children from the tree asynchronously.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="updateSelection">Whether selection should be updated after removal.</param>
    /// <remarks>
    ///     Removal is reported via <see cref="ItemRemoved"/> with a <see cref="TreeItemRemovedEventArgs.RelativeIndex"/>
    ///     that represents the removed item's position in its parent's children collection <em>before</em> removal.
    ///     This is the correct index to use for undo via <see cref="InsertItemAsync"/>.
    ///     <para>
    ///     Do not attempt to use <see cref="ShownItems"/> indices for undo/redo; <see cref="ShownItems"/> varies
    ///     with expansion/collapse and virtualization.</para>
    /// </remarks>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task RemoveItemAsync(ITreeItem item, bool updateSelection = true)
        => await this.DisplayHelper.RemoveItemAsync(item, updateSelection).ConfigureAwait(true);

    /// <summary>
    ///     Removes the currently selected items from the tree asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method supports both single- and multiple-selection models. If there is no
    ///     selection, the method returns immediately. Selection is cleared before modification so
    ///     that shown-item indices remain valid while items are removed. In single-selection mode,
    ///     the single selected item is removed if it is not locked. In multiple-selection mode, all
    ///     selected items that are not locked are removed; selection updates are deferred during
    ///     batch removes to avoid a burst of selection change events.
    /// </remarks>
    public virtual async Task RemoveSelectedItems()
        => await this.DisplayHelper.RemoveSelectedItemsAsync().ConfigureAwait(true);

    /// <summary>
    ///     Moves an item to a different parent and position within the tree asynchronously.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The new parent under which the item is inserted.</param>
    /// <param name="newIndex">The zero-based insertion index beneath <paramref name="newParent"/>.</param>
    /// <remarks>
    ///     The index parameters for move operations are expressed in the coordinate system of the
    ///     underlying children collections (<see cref="ITreeItem.Children"/>), not in <see cref="ShownItems"/>.
    ///     <para>
    ///     <paramref name="newIndex"/> is interpreted as an insertion point in <paramref name="newParent"/>'s current
    ///     children list at the time the move request is evaluated (that is, before any moved items are detached).
    ///     For moves within the same parent, this avoids off-by-one errors caused by index shifting when the item is
    ///     removed and reinserted.</para>
    ///     <para>
    ///     The completed operation is reported via <see cref="ItemMoved"/>; consumers should prefer
    ///     <see cref="MovedItemInfo.PreviousIndex"/> and <see cref="MovedItemInfo.NewIndex"/> from that event for
    ///     undo/redo, because those reflect the actual indices used.</para>
    /// </remarks>
    /// <returns>A task that completes once the move finishes.</returns>
    public Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
        => this.DisplayHelper.MoveItemAsync(item, newParent, newIndex);

    /// <summary>
    ///     Moves a batch of items into a new parent while preserving their relative order.
    /// </summary>
    /// <param name="items">The list of items to move.</param>
    /// <param name="newParent">The parent that receives the moved items.</param>
    /// <param name="startIndex">The index within <paramref name="newParent" /> where the first item is inserted.</param>
    /// <remarks>
    ///     <paramref name="startIndex"/> uses the same semantics as <see cref="MoveItemAsync"/>: it is an insertion index in
    ///     the target parent's current children list before detaching any of the moved items.
    ///     <para>
    ///     A single <see cref="ItemMoved"/> event is raised after all items are relocated.
    ///     Use the <see cref="TreeItemsMovedEventArgs.Moves"/> entries for reliable undo/redo.</para>
    /// </remarks>
    /// <returns>A task that completes once the batch move finishes.</returns>
    public Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
        => this.DisplayHelper.MoveItemsAsync(items, newParent, startIndex);

    /// <summary>
    ///     Reorders a single item among its siblings asynchronously.
    /// </summary>
    /// <param name="item">The item whose shown position should change.</param>
    /// <param name="newIndex">The new zero-based index among the current parent&apos;s children.</param>
    /// <returns>A task that completes once the reorder finishes.</returns>
    public Task ReorderItemAsync(ITreeItem item, int newIndex)
        => this.DisplayHelper.ReorderItemAsync(item, newIndex);

    /// <summary>
    ///     Reorders a contiguous block of shown items beneath their current parent asynchronously.
    /// </summary>
    /// <param name="items">The ordered list of items to reposition.</param>
    /// <param name="startIndex">The destination index of the first item in <paramref name="items" />.</param>
    /// <returns>A task that completes once the block reorder finishes.</returns>
    public Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
        => this.DisplayHelper.ReorderItemsAsync(items, startIndex);

    /// <summary>
    ///     Clears the current focus from the tree, setting <see cref="FocusedItem"/> to <see langword="null"/>.
    /// </summary>
    public void ClearFocus() => this.FocusedItem = null;

    /// <summary>
    ///     Sets focus to the specified item if it is visible.
    /// </summary>
    /// <param name="item">The item to focus. This parameter must not be <see langword="null" />.</param>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> when focus changed successfully; otherwise, <see langword="false" />.</returns>
    public bool FocusItem(ITreeItem item, RequestOrigin origin)
    {
        ArgumentNullException.ThrowIfNull(item);

        this.LogFocusItemCalled(item, origin);

        if (!this.shownItems.Contains(item))
        {
            return false;
        }

        this.FocusedItem = new(item, origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the next visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusNextVisibleItem(RequestOrigin origin)
    {
        if (!this.EnsureFocus(origin))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var currentIndex = this.shownItems.IndexOf(this.FocusedItem.Item);
        if (currentIndex == -1 || currentIndex >= this.shownItems.Count - 1)
        {
            return false;
        }

        this.FocusedItem = new(this.shownItems[currentIndex + 1], origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the previous visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusPreviousVisibleItem(RequestOrigin origin)
    {
        if (!this.EnsureFocus(origin))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var currentIndex = this.shownItems.IndexOf(this.FocusedItem.Item);
        if (currentIndex <= 0)
        {
            return false;
        }

        this.FocusedItem = new(this.shownItems[currentIndex - 1], origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the first visible item that shares the same parent as the currently focused item.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus changed; otherwise, <see langword="false" />.</returns>
    public bool FocusFirstVisibleItemInParent(RequestOrigin origin)
    {
        if (!this.EnsureFocus(origin))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var target = this.FindSibling(this.FocusedItem.Item.Parent, first: true);
        if (target is null)
        {
            return false;
        }

        this.FocusedItem = new(target, origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the last visible item that shares the same parent as the currently focused item.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus changed; otherwise, <see langword="false" />.</returns>
    public bool FocusLastVisibleItemInParent(RequestOrigin origin)
    {
        if (!this.EnsureFocus(origin))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var target = this.FindSibling(this.FocusedItem.Item.Parent, first: false);
        if (target is null)
        {
            return false;
        }

        this.FocusedItem = new(target, origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the first visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusFirstVisibleItemInTree(RequestOrigin origin)
    {
        if (this.shownItems.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        this.FocusedItem = new(this.shownItems[0], origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the last visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusLastVisibleItemInTree(RequestOrigin origin)
    {
        if (this.shownItems.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        this.FocusedItem = new(this.shownItems[^1], origin);
        return true;
    }

    /// <summary>
    ///     Expands the currently focused item when it can accept children.
    /// </summary>
    /// <returns><see langword="true" /> when the item was expanded; otherwise, <see langword="false" />.</returns>
    public async Task<bool> ExpandFocusedItemAsync()
    {
        if (!this.EnsureFocus(RequestOrigin.Programmatic))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var focused = this.FocusedItem.Item;
        if (focused.IsExpanded || !focused.CanAcceptChildren)
        {
            return false;
        }

        await this.ExpandItemAsync(focused).ConfigureAwait(true);

        // Ensure the view reapplies focus for the currently focused item,
        // preserving the original request origin when available.
        var origin = this.FocusedItem?.Origin ?? RequestOrigin.Programmatic;
        _ = this.FocusItem(focused, origin);
        return true;
    }

    /// <summary>
    ///     Collapses the currently focused item when it is expanded.
    /// </summary>
    /// <returns><see langword="true" /> when the item was collapsed; otherwise, <see langword="false" />.</returns>
    public async Task<bool> CollapseFocusedItemAsync()
    {
        if (!this.EnsureFocus(RequestOrigin.Programmatic))
        {
            return false;
        }

        Debug.Assert(this.FocusedItem is not null, "EnsureFocus should guarantee FocusedItem is not null");
        var focused = this.FocusedItem.Item;
        if (!focused.IsExpanded)
        {
            return false;
        }

        await this.CollapseItemAsync(focused).ConfigureAwait(true);

        // Ensure the view reapplies focus for the currently focused item,
        // preserving the original request origin when available.
        var origin = this.FocusedItem?.Origin ?? RequestOrigin.Programmatic;
        _ = this.FocusItem(focused, origin);
        return true;
    }

    /// <summary>
    ///     Releases resources used by the view model related to filtering.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Updates the currently focused item together with the origin of the request.
    /// </summary>
    /// <param name="item">The tree item to mark as focused.</param>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <remarks>
    ///     This method sets the underlying focused item field directly to avoid raising property-changed
    ///     notifications or other side effects that are associated with the <see cref="FocusedItem"/> property.
    /// </remarks>
    internal void UpdateFocusedItem(ITreeItem item, RequestOrigin origin)
    {
        if (this.FocusedItem is not null && ReferenceEquals(this.FocusedItem.Item, item))
        {
            return;
        }

        // We explicitly do not use the property here to avoid side effects.
        this.focusedItem = new(item, origin);
    }

    /// <summary>
    ///     Ensures there is a focused item by preferring the current focus, then the selected item,
    ///     and finally the first shown item.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if a focusable item was found; otherwise, <see langword="false" />.</returns>
    protected internal bool EnsureFocus(RequestOrigin origin)
    {
        if (this.focusedItem is not null && this.shownItems.Contains(this.focusedItem.Item))
        {
            Debug.WriteLine("Focus already valid on item: " + this.focusedItem.Item.Label);
            return true;
        }

        var selected = this.SelectionModel?.SelectedItem;
        if (selected is not null && this.shownItems.Contains(selected))
        {
            Debug.WriteLine("Focusing selected item: " + selected.Label);
            this.FocusedItem = new(selected, origin);
            return true;
        }

        if (this.shownItems.Count > 0)
        {
            Debug.WriteLine("Focusing first shown item: " + this.shownItems[0].Label);
            this.FocusedItem = new(this.shownItems[0], origin);
            return true;
        }

        Debug.WriteLine("No focusable item found; clearing focus");
        this.FocusedItem = null;
        return false;
    }

    /// <summary>
    ///     Focuses the shown item that best matches the specified text.
    /// </summary>
    /// <param name="text">The text to match against item labels.</param>
    /// <param name="origin">The origin of the focus request.</param>
    /// <returns><see langword="true"/> if a matching item was found and focused; otherwise <see langword="false"/>.</returns>
    protected internal bool FocusNextByPrefix(string text, RequestOrigin origin)
    {
        if (this.shownItems.Count == 0)
        {
            return false;
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var startIndex = 0;
        if (this.FocusedItem is { Item: { } focused })
        {
            var focusedIndex = this.shownItems.IndexOf(focused);
            if (focusedIndex >= 0)
            {
                startIndex = (focusedIndex + 1) % this.shownItems.Count;
            }
        }

        var bestScore = int.MinValue;
        var bestOffset = int.MaxValue;
        ITreeItem? bestItem = null;

        for (var offset = 0; offset < this.shownItems.Count; offset++)
        {
            var index = (startIndex + offset) % this.shownItems.Count;
            var item = this.shownItems[index];

            var score = GetTypeAheadScore(item.Label, text, out var matchedCount);
            if (matchedCount == 0)
            {
                continue;
            }

            // Higher score wins; ties prefer the next match after current focus (smallest offset).
            if (score > bestScore || (score == bestScore && offset < bestOffset))
            {
                bestScore = score;
                bestOffset = offset;
                bestItem = item;
            }
        }

        return bestItem is not null && this.FocusItem(bestItem, origin);
    }

    /// <summary>
    ///     Returns the index of the given item in the shown list, or -1 when not shown.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns>The 0-based index of the item if it is shown; otherwise -1.</returns>
    protected internal int ShownIndexOf(ITreeItem item) => this.shownItems.IndexOf(item);

    /// <summary>
    ///     Gets the shown item at the specified index.
    /// </summary>
    /// <param name="index">The 0-based index of the item.</param>
    /// <returns>The shown item at <paramref name="index"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when <paramref name="index"/> is outside the shown range.</exception>
    protected internal ITreeItem GetShownItemAt(int index) => this.shownItems[index];

    /// <summary>
    ///     Returns whether the given item is currently shown.
    /// </summary>
    /// <param name="item">The item to check.</param>
    /// <returns><see langword="true"/> if the item is currently shown; otherwise <see langword="false"/>.</returns>
    protected internal bool IsShown(ITreeItem item) => this.shownItems.Contains(item);

    /// <summary>
    ///     Releases the resources used by the view model related to filtering.
    /// </summary>
    /// <param name="disposing">If <see langword="true"/>, release managed resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            if (this.filteredItems is not null)
            {
                // We own a manual-refresh view; unsubscribe and dispose it.
                this.shownItems.CollectionChanged -= this.OnShownItemsCollectionChangedForFiltering;
                this.filteredItems.Dispose();
                this.filteredItems = null;
            }

            // Unsubscribe observed item property changed handlers.
            foreach (var notify in this.observedItems)
            {
                notify.PropertyChanged -= this.OnShownItemPropertyChangedForFiltering;
            }

            this.observedItems.Clear();
            this.includedItems.Clear();
        }

        this.disposed = true;
    }

    private static int GetTypeAheadScore(string label, string query, out int matchedCount)
    {
        if (string.IsNullOrEmpty(label) || string.IsNullOrEmpty(query))
        {
            matchedCount = 0;
            return 0;
        }

        var score = ScoreSubsequenceMatches(label, query, out matchedCount);
        if (matchedCount == 0)
        {
            return 0;
        }

        if (matchedCount == query.Length)
        {
            score += 100;
        }

        score -= Math.Min(label.Length, 50);
        return score;
    }

    private static int ScoreSubsequenceMatches(string label, string query, out int matchedCount)
    {
        matchedCount = 0;
        var score = 0;
        var labelIndex = 0;
        var lastMatchIndex = -2;

        for (var queryIndex = 0; queryIndex < query.Length; queryIndex++)
        {
            var queryChar = char.ToUpperInvariant(query[queryIndex]);
            var foundIndex = IndexOfCharIgnoreCase(label, queryChar, labelIndex);
            if (foundIndex < 0)
            {
                break;
            }

            matchedCount++;
            score += 10;

            if (foundIndex == lastMatchIndex + 1)
            {
                score += 6;
            }

            if (foundIndex == 0)
            {
                score += 4;
            }
            else
            {
                var prev = label[foundIndex - 1];
                if (char.IsWhiteSpace(prev) || prev is '_' or '-' or '.' or '/' or '\\')
                {
                    score += 3;
                }
            }

            lastMatchIndex = foundIndex;
            labelIndex = foundIndex + 1;
        }

        return score;
    }

    private static int IndexOfCharIgnoreCase(string text, char upperChar, int startIndex)
    {
        for (var i = startIndex; i < text.Length; i++)
        {
            if (char.ToUpperInvariant(text[i]) == upperChar)
            {
                return i;
            }
        }

        return -1;
    }

    /// <summary>
    ///     Toggles the expansion state of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to toggle.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     If the item is currently expanded, it will be collapsed. If it is currently collapsed, it
    ///     will be expanded.
    /// </remarks>
    [RelayCommand]
    private async Task ToggleExpanded(TreeItemAdapter itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            await this.CollapseItemAsync(itemAdapter).ConfigureAwait(true);
        }
        else
        {
            await this.ExpandItemAsync(itemAdapter).ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Restores the expanded state of the children of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose children should be restored.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method inserts the children of the specified item into the shown items collection,
    ///     maintaining their expanded state.
    /// </remarks>
    private async Task RestoreExpandedChildrenAsync(ITreeItem itemAdapter)
    {
        this.LogRestoreExpandedChildrenStarted(itemAdapter);
        var insertIndex = this.shownItems.IndexOf(itemAdapter) + 1;
        _ = await this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex).ConfigureAwait(true);
    }

    /// <summary>
    ///     Recursively restores the expanded state of the children of the specified tree item.
    /// </summary>
    /// <param name="parent">The parent tree item whose children should be restored.</param>
    /// <param name="insertIndex">The index at which to start inserting the restored children in the ShownItems collection.</param>
    /// <returns>
    ///     A task representing the asynchronous operation, with the final insert index after all children have been
    ///     processed.
    /// </returns>
    /// <remarks>
    ///     This method inserts the children of the specified parent item into the ShownItems
    ///     collection, maintaining their expanded state. If a child item is expanded, the method is
    ///     called recursively to restore its children as well.
    /// </remarks>
    private async Task<int> RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(true))
        {
            this.LogShownItemsInsert(insertIndex, child);
            this.shownItems.Insert(insertIndex, (TreeItemAdapter)child);
            ++insertIndex;

            if (child.IsExpanded)
            {
                insertIndex = await this.RestoreExpandedChildrenRecursive(child, insertIndex).ConfigureAwait(true);
            }
        }

        return insertIndex;
    }

    /// <summary>
    ///     Hides the children of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose children should be hidden.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method removes the children of the specified item from the shown items collection,
    ///     maintaining their collapsed state.
    /// </remarks>
    private async Task HideChildrenAsync(ITreeItem itemAdapter)
    {
        this.LogHideChildrenStarted(itemAdapter);
        var removeIndex = this.shownItems.IndexOf((TreeItemAdapter)itemAdapter) + 1;
        Debug.Assert(removeIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");

        await this.HideChildrenRecursiveAsync(itemAdapter).ConfigureAwait(true);
    }

    /// <summary>
    ///     Recursively hides the children of the specified tree item.
    /// </summary>
    /// <param name="parent">The parent tree item whose children should be hidden.</param>
    /// <remarks>The method calculates child indices based on the current shown items.</remarks>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method removes the children of the specified parent item from the ShownItems collection,
    ///     maintaining their collapsed state. If a child item is expanded, the method is called recursively
    ///     to hide its children as well.
    /// </remarks>
    private async Task HideChildrenRecursiveAsync(ITreeItem parent)
    {
        var children = (await parent.Children.ConfigureAwait(true)).ToList();

        for (var i = children.Count - 1; i >= 0; i--)
        {
            var child = children[i];

            if (ReferenceEquals(this.FocusedItem, child))
            {
                this.FocusedItem = new(parent, RequestOrigin.Programmatic);
            }

            // Find the current index of the child in the shown list.
            var childIndex = this.shownItems.IndexOf(child);
            if (childIndex == -1)
            {
                // Not shown; skip
                continue;
            }

            // First hide descendants (if expanded) starting at the index after the child.
            if (child.IsExpanded)
            {
                await this.HideChildrenRecursiveAsync(child).ConfigureAwait(true);
            }

            // Try to clear selection for the child and any selection state.
            var selection = this.SelectionModel;
            if (selection is not null)
            {
                try
                {
                    selection.ClearSelection(child);
                }
                catch (ArgumentException)
                {
                    // Item not found in selection; ignore.
                }
            }

            this.LogShownItemsRemoveAt(childIndex);
            this.shownItems.RemoveAt(childIndex);
        }
    }

    private ITreeItem? FindSibling(ITreeItem? parent, bool first)
    {
        ITreeItem? target = null;
        for (var index = 0; index < this.shownItems.Count; index++)
        {
            var item = this.shownItems[index];
            if (!ReferenceEquals(item.Parent, parent))
            {
                continue;
            }

            if (first)
            {
                return item;
            }

            target = item;
        }

        return target;
    }

    /// <summary>
    /// Represents the currently focused item together with the origin of the focus request.
    /// </summary>
    /// <param name="Item">The tree item that currently holds logical focus.</param>
    /// <param name="Origin">The origin of the focus request <see cref="RequestOrigin"/>.</param>
    /// <remarks>
    /// Equality is based on the focused <see cref="ITreeItem"/> reference only; the
    /// <see cref="Origin"/> is not considered for equality comparisons.
    /// </remarks>
    protected internal record FocusedItemInfo(ITreeItem Item, RequestOrigin Origin)
    {
        /// <inheritdoc/>
        public virtual bool Equals(FocusedItemInfo? other)
            => ReferenceEquals(this.Item, other?.Item);

        /// <inheritdoc/>
        public override int GetHashCode()
            => this.Item?.GetHashCode() ?? 0;
    }
}
