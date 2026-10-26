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
///     <para>
///     A view model is shown by one live <see cref="DynamicTree"/> at a time. Its interaction state, which is the
///     logical keyboard focus, the scope and displayed source that keyboard navigation and typeahead walk, and the
///     bring-into-view requests, belongs to the tree that shows it. A second tree loaded over the same view
///     model while the first is still loaded is rejected, in every build configuration: it shows no rows and the
///     view model logs an error. A tree lets go when it unloads or moves to another view model, and another tree can then attach.
///     Expansion and selection live on the items, so they are the same wherever the items show.</para>
/// </remarks>
public abstract partial class DynamicTreeViewModel(ILoggerFactory? loggerFactory = null) : ObservableObject, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<DynamicTreeViewModel>() ?? NullLogger<DynamicTreeViewModel>.Instance;
    private readonly ObservableCollection<ITreeItem> shownItems = [];
    private readonly Dictionary<ITreeItem, Task> expansionsInFlight = [with(ReferenceEqualityComparer.Instance)];

    private TreeDisplayHelper? displayHelper;
    private FocusedItemInfo? focusedItem;
    private DynamicTree? attachedTree;

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
    ///     Gets or sets the scope that keyboard focus and typeahead address.
    /// </summary>
    /// <remarks>
    ///     The owning control assigns this from its <c>SelectionScope</c> and supplies its displayed source
    ///     whenever that scope or source changes. Standalone displayed-scope navigation uses <see cref="FilteredItems"/>.
    ///     <see cref="TreeSelectionScope.ShownItems"/>, the default, makes keyboard navigation and typeahead walk
    ///     <see cref="ShownItems"/>.
    ///     <see cref="TreeSelectionScope.DisplayedItems"/> restricts them to the displayed
    ///     projection, so a keyboard user cannot land on a row that the filter hid while pointer
    ///     selection is already scoped to it. The control uses <see cref="SelectDisplayedItem"/> for selection ranges;
    ///     direct view-model selection commands retain their unfiltered semantics.
    /// </remarks>
    public TreeSelectionScope InteractionScope { get; set; } = TreeSelectionScope.ShownItems;

    /// <summary>
    ///     Gets the <see cref="ILoggerFactory"/> used to create loggers for this view model.
    /// </summary>
    internal ILoggerFactory? LoggerFactory { get; } = loggerFactory;

    /// <summary>Gets or sets the attached tree's displayed source, when a tree is attached.</summary>
    internal IEnumerable<ITreeItem>? DisplayedInteractionItems { get; set; }

    /// <summary>
    ///     Gets the items keyboard focus and typeahead may move between.
    /// </summary>
    private IList<ITreeItem> NavigableItems
    {
        get
        {
            if (this.InteractionScope != TreeSelectionScope.DisplayedItems)
            {
                return this.shownItems;
            }

            // FilteredItems degrades to the shown collection when no predicate is installed.
            var displayed = this.DisplayedInteractionItems ?? this.FilteredItems;
            if (ReferenceEquals(displayed, this.shownItems))
            {
                return this.shownItems;
            }

            var items = new List<ITreeItem>();
            foreach (var item in displayed)
            {
                items.Add(item);
            }

            return items;
        }
    }

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

                    return args.Proceed;
                },
                args => this.ItemAdded?.Invoke(this, args),
                args =>
                {
                    this.ItemBeingRemoved?.Invoke(this, args);

                    return args.Proceed;
                },
                args =>
                {
                    this.InvalidateClipboardIfSourcesAffected([args.TreeItem]);
                    this.ItemRemoved?.Invoke(this, args);
                },
                args =>
                {
                    this.ItemBeingMoved?.Invoke(this, args);

                    return args.Proceed;
                },
                args =>
                {
                    this.InvalidateClipboardIfSourcesAffected(args.Moves.Select(static move => move.Item));
                    this.ItemMoved?.Invoke(this, args);
                }),
            this.LoggerFactory);

    /// <summary>
    ///     Expands the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to expand.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     An item that is not itself shown, for example a collapsed descendant of a collapsed branch, only
    ///     records its expanded state; its children show when the branch above it expands. Expanding an item
    ///     that is already being expanded joins the expansion in progress, so its children are shown once.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown when the item's parent is not expanded.</exception>
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

        if (this.expansionsInFlight.TryGetValue(itemAdapter, out var pending))
        {
            await pending.ConfigureAwait(true);
            return;
        }

        var expansion = this.ExpandShownItemAsync(itemAdapter);
        this.expansionsInFlight[itemAdapter] = expansion;
        try
        {
            await expansion.ConfigureAwait(true);
        }
        finally
        {
            _ = this.expansionsInFlight.Remove(itemAdapter);
        }
    }

    /// <summary>
    ///     Collapses the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to collapse.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     If the item is already collapsed, this method does nothing. Focus that sat on a hidden descendant
    ///     moves to the collapsed item.
    /// </remarks>
    public Task CollapseItemAsync(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return Task.CompletedTask;
        }

        this.HideChildren(itemAdapter);
        itemAdapter.IsExpanded = false;
        return Task.CompletedTask;
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
        this.ClearRoot();

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
            await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(true);
        }

        this.ResetFilteringState();
        this.SyncSelectionModelWithItems();

        // Focus established by (re)initialization must not scroll: the control brings a row into view for every origin
        // but a pointer's, and a reconciliation re-initializes the tree while the user is looking elsewhere.
        _ = this.EnsureFocus(RequestOrigin.PointerInput);
    }

    /// <summary>Releases displayed rows, selection and focus without installing another root.</summary>
    /// <remarks>The clipboard is retained; its domain owner decides whether a document transition invalidates it.</remarks>
    public void ClearRoot()
    {
        if (this.SelectionModel is { IsEmpty: false } || this.ActiveItem is not null)
        {
            this.WithSelectionBatch(
                () =>
                {
                    this.SelectionModel?.ClearSelection();
                    this.SetActiveItem(item: null);
                });
        }

        this.displayedSelectionAnchor = null;
        this.ClearFocus();
        this.LogShownItemsClear();
        this.shownItems.Clear();
        this.ResetFilteringState();
        this.SyncSelectionModelWithItems();
    }

    /// <summary>
    ///     Inserts a child item at the specified index under the given parent item asynchronously.
    /// </summary>
    /// <param name="item">The child item to insert.</param>
    /// <param name="parent">The parent that receives the child.</param>
    /// <param name="relativeIndex">Zero-based index at which to insert under the parent.</param>
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
    public virtual async Task InsertItemAsync(ITreeItem item, ITreeItem parent, int relativeIndex)
        => await this.DisplayHelper.InsertItemAsync(item, parent, relativeIndex).ConfigureAwait(true);

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
    public virtual async Task RemoveItemAsync(ITreeItem item, bool updateSelection = true)
    {
        var focusIndex = this.FocusedShownIndex();
        _ = await this.DisplayHelper.RemoveItemAsync(item, updateSelection).ConfigureAwait(true);
        this.ForgetRemovedSelectionState();
        this.RepairFocusAfterRemoval(focusIndex);
    }

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
    {
        var focusIndex = this.FocusedShownIndex();
        await this.DisplayHelper.RemoveSelectedItemsAsync().ConfigureAwait(true);
        this.ForgetRemovedSelectionState();
        this.RepairFocusAfterRemoval(focusIndex);
    }

    /// <summary>Commits a validated inline rename for a tree item.</summary>
    /// <param name="item">The item captured when rename began.</param>
    /// <param name="newName">The proposed label.</param>
    /// <returns>Whether the rename was committed, with optional user-facing rejection text.</returns>
    public virtual Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(newName);
        if (!this.IsShown(item))
        {
            return Task.FromResult(TreeItemRenameResult.Rejected("The item is no longer available in this tree."));
        }

        var normalizedName = newName.Trim();
        if (!item.ValidateItemName(normalizedName))
        {
            return Task.FromResult(TreeItemRenameResult.Rejected("The name is not valid."));
        }

        if (!string.Equals(item.Label, normalizedName, StringComparison.Ordinal))
        {
            item.Label = normalizedName;
        }

        return Task.FromResult(TreeItemRenameResult.Success);
    }

    /// <summary>Commits a drag/drop intent and returns the resulting roots.</summary>
    /// <remarks>
    /// Override this method when the backing application must validate or commit domain state
    /// before changing the tree projection. The base implementation provides ordinary tree move
    /// and duplicate behavior for adapter-backed trees.
    /// </remarks>
    /// <param name="request">The stable source identities and resolved destination.</param>
    /// <returns>The resulting items, or a rejected result when the request is no longer valid.</returns>
    public virtual async Task<TreeDropResult> CommitDropAsync(TreeDropRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.Items.Count == 0 || request.Items.Any(item => !this.IsShown(item) || item.IsLocked)
            || !this.CanShowChildrenOf(request.Parent) || !request.Parent.CanAcceptChildren || (request.Parent.IsLocked && !request.Parent.IsRoot))
        {
            return TreeDropResult.Rejected;
        }

        if (request.Operation == TreeDropOperation.Copy)
        {
            var copies = await this.DuplicateItemsAsync(request.Items, request.Parent, request.Index).ConfigureAwait(true);
            return copies.Count == 0 ? TreeDropResult.Rejected : TreeDropResult.Committed(copies);
        }

        var result = TreeDropResult.Rejected;
        void OnMoved(object? sender, TreeItemsMovedEventArgs args)
        {
            if (args.Moves.All(move => request.Items.Contains(move.Item)))
            {
                result = TreeDropResult.Committed([.. args.Moves.Select(static move => move.Item)]);
            }
        }

        this.ItemMoved += OnMoved;
        try
        {
            await this.MoveItemsAsync(request.Items, request.Parent, request.Index).ConfigureAwait(true);
        }
        finally
        {
            this.ItemMoved -= OnMoved;
        }

        return result;
    }

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
    public virtual Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
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
    public virtual Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
        => this.DisplayHelper.MoveItemsAsync(items, newParent, startIndex);

    /// <summary>
    ///     Reorders a single item among its siblings asynchronously.
    /// </summary>
    /// <param name="item">The item whose shown position should change.</param>
    /// <param name="newIndex">The new zero-based index among the current parent&apos;s children.</param>
    /// <returns>A task that completes once the reorder finishes.</returns>
    public virtual Task ReorderItemAsync(ITreeItem item, int newIndex)
        => this.DisplayHelper.ReorderItemAsync(item, newIndex);

    /// <summary>
    ///     Reorders a contiguous block of shown items beneath their current parent asynchronously.
    /// </summary>
    /// <param name="items">The ordered list of items to reposition.</param>
    /// <param name="startIndex">The destination index of the first item in <paramref name="items" />.</param>
    /// <returns>A task that completes once the block reorder finishes.</returns>
    public virtual Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
        => this.DisplayHelper.ReorderItemsAsync(items, startIndex);

    /// <summary>
    ///     Clears the current focus from the tree, setting <see cref="FocusedItem"/> to <see langword="null"/>.
    /// </summary>
    public void ClearFocus() => this.FocusedItem = null;

    /// <summary>
    ///     Scrolls a shown item into view without moving keyboard focus, for example to follow a
    ///     selection made in another panel.
    /// </summary>
    /// <param name="item">The item to show.</param>
    /// <returns><see langword="true" /> when the item is shown and the request was raised.</returns>
    public bool BringItemIntoView(ITreeItem item)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (!this.shownItems.Contains(item))
        {
            return false;
        }

        this.BringIntoViewRequested?.Invoke(this, new DynamicTreeEventArgs { TreeItem = item });
        return true;
    }

    /// <summary>
    ///     Invokes an item, raising <see cref="ItemInvoked"/>.
    /// </summary>
    /// <param name="item">The invoked item.</param>
    public void InvokeItem(ITreeItem item)
    {
        ArgumentNullException.ThrowIfNull(item);
        this.ItemInvoked?.Invoke(this, new DynamicTreeEventArgs { TreeItem = item });
    }

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
        var items = this.NavigableItems;
        if (items.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        if (this.InteractionScope != TreeSelectionScope.DisplayedItems && !this.EnsureFocus(origin))
        {
            return false;
        }

        var currentIndex = this.FocusedItem is { } focused ? items.IndexOf(focused.Item) : -1;
        if (currentIndex == -1)
        {
            // Enter the scope at its boundary when focus is absent or filtered out.
            this.FocusedItem = new(items[0], origin);
            return true;
        }

        if (currentIndex >= items.Count - 1)
        {
            return false;
        }

        this.FocusedItem = new(items[currentIndex + 1], origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the previous visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusPreviousVisibleItem(RequestOrigin origin)
    {
        var items = this.NavigableItems;
        if (items.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        if (this.InteractionScope != TreeSelectionScope.DisplayedItems && !this.EnsureFocus(origin))
        {
            return false;
        }

        var currentIndex = this.FocusedItem is { } focused ? items.IndexOf(focused.Item) : -1;
        if (currentIndex == -1)
        {
            this.FocusedItem = new(items[^1], origin);
            return true;
        }

        if (currentIndex <= 0)
        {
            return false;
        }

        this.FocusedItem = new(items[currentIndex - 1], origin);
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
        var items = this.NavigableItems;
        if (items.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        this.FocusedItem = new(items[0], origin);
        return true;
    }

    /// <summary>
    ///     Moves focus to the last visible item in the tree.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if focus moved; otherwise, <see langword="false" />.</returns>
    public bool FocusLastVisibleItemInTree(RequestOrigin origin)
    {
        var items = this.NavigableItems;
        if (items.Count == 0)
        {
            this.FocusedItem = null;
            return false;
        }

        this.FocusedItem = new(items[^1], origin);
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
    ///     Attaches <paramref name="tree"/> as the one live tree of this view model.
    /// </summary>
    /// <param name="tree">The tree that is loaded with this view model.</param>
    /// <returns>
    ///     <see langword="true"/> when the tree is attached; <see langword="false"/> when another loaded tree already is,
    ///     in which case an error is logged and the rejected tree shows no rows.
    /// </returns>
    internal bool TryAttachTree(DynamicTree tree)
    {
        if (this.attachedTree is { IsLoaded: true } current && !ReferenceEquals(current, tree))
        {
            this.LogSecondTreeRejected();
            return false;
        }

        this.attachedTree = tree;
        return true;
    }

    /// <summary>
    ///     Lets go of <paramref name="tree"/> and of the displayed source it supplied, when it is the attached tree.
    /// </summary>
    /// <param name="tree">The tree that unloads or moves to another view model.</param>
    internal void DetachTree(DynamicTree tree)
    {
        if (ReferenceEquals(this.attachedTree, tree))
        {
            this.attachedTree = null;
            this.DisplayedInteractionItems = null;
        }
    }

    /// <summary>Gets a value indicating whether <paramref name="tree"/> is the attached tree.</summary>
    /// <param name="tree">The tree.</param>
    /// <returns><see langword="true"/> when <paramref name="tree"/> is attached to this view model.</returns>
    internal bool IsAttachedTree(DynamicTree tree) => ReferenceEquals(this.attachedTree, tree);

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
    ///     and finally the first item in the interaction scope.
    /// </summary>
    /// <param name="origin">The origin of the focus request (pointer, keyboard, or programmatic).</param>
    /// <returns><see langword="true" /> if a focusable item was found; otherwise, <see langword="false" />.</returns>
    protected internal bool EnsureFocus(RequestOrigin origin)
    {
        var items = this.NavigableItems;
        if (this.focusedItem is not null && items.Contains(this.focusedItem.Item))
        {
            return true;
        }

        var selected = this.SelectionModel?.SelectedItem;
        if (selected is not null && items.Contains(selected))
        {
            this.FocusedItem = new(selected, origin);
            return true;
        }

        if (items.Count > 0)
        {
            this.FocusedItem = new(items[0], origin);
            return true;
        }

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
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var items = this.NavigableItems;
        if (items.Count == 0)
        {
            return false;
        }

        var startIndex = 0;
        if (this.FocusedItem is { Item: { } focused })
        {
            var focusedIndex = items.IndexOf(focused);
            if (focusedIndex >= 0)
            {
                startIndex = (focusedIndex + 1) % items.Count;
            }
        }

        var bestScore = int.MinValue;
        var bestOffset = int.MaxValue;
        ITreeItem? bestItem = null;

        for (var offset = 0; offset < items.Count; offset++)
        {
            var index = (startIndex + offset) % items.Count;
            var item = items[index];

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
            this.DisposeFilteringSubscriptions();
            this.filteredItems?.Dispose();
            this.filteredItems = null;
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
    private async Task ToggleExpanded(ITreeItem itemAdapter)
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
    ///     Expands <paramref name="item"/> once its children are known, showing them only if the item is still shown.
    /// </summary>
    /// <param name="item">The collapsed item to expand.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    private async Task ExpandShownItemAsync(ITreeItem item)
    {
        if (!this.IsShown(item))
        {
            // A collapsed descendant of a collapsed branch: its rows join the shown list when the branch expands.
            this.LogExpandItemNotShown(item);
            item.IsExpanded = true;
            return;
        }

        this.LogExpandItem(item);
        var descendants = new List<ITreeItem>();
        await this.CollectShownDescendantsAsync(item, descendants).ConfigureAwait(true);

        // Loading children awaits; the item may have left the tree meanwhile, and its position is taken only now.
        if (this.IsShown(item))
        {
            this.InsertShownDescendants(item, descendants);
        }

        item.IsExpanded = true;
    }

    /// <summary>
    ///     Shows the children of <paramref name="itemAdapter"/> and, for each expanded child, its own expanded subtree.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose children should be restored.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     A hidden root, which is not itself in the shown items collection, has its children inserted at the start.
    /// </remarks>
    private async Task RestoreExpandedChildrenAsync(ITreeItem itemAdapter)
    {
        this.LogRestoreExpandedChildrenStarted(itemAdapter);
        var descendants = new List<ITreeItem>();
        await this.CollectShownDescendantsAsync(itemAdapter, descendants).ConfigureAwait(true);
        this.InsertShownDescendants(itemAdapter, descendants);
    }

    /// <summary>
    ///     Collects, in display order, the children of <paramref name="parent"/> and the expanded subtrees below them.
    /// </summary>
    /// <param name="parent">The parent whose children are collected.</param>
    /// <param name="descendants">Receives the items that show when <paramref name="parent"/> is expanded.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     Loading children may await, so this only reads: the shown collection is changed afterwards in one
    ///     synchronous step, with positions that cannot have gone stale.
    /// </remarks>
    private async Task CollectShownDescendantsAsync(ITreeItem parent, List<ITreeItem> descendants)
    {
        foreach (var child in (await parent.Children.ConfigureAwait(true)).ToArray())
        {
            descendants.Add(child);
            if (child.IsExpanded)
            {
                await this.CollectShownDescendantsAsync(child, descendants).ConfigureAwait(true);
            }
        }
    }

    /// <summary>
    ///     Inserts <paramref name="descendants"/> right after <paramref name="parent"/> in the shown items collection.
    /// </summary>
    /// <param name="parent">The item the rows belong under; a hidden root has no row, so they go first.</param>
    /// <param name="descendants">The rows to show, in display order.</param>
    private void InsertShownDescendants(ITreeItem parent, List<ITreeItem> descendants)
    {
        if (descendants.Count == 0)
        {
            return;
        }

        var insertIndex = this.shownItems.IndexOf(parent) + 1;
        this.ReshapeShownItems(
            () =>
            {
                foreach (var descendant in descendants)
                {
                    this.LogShownItemsInsert(insertIndex, descendant);
                    this.shownItems.Insert(insertIndex, descendant);
                    ++insertIndex;
                }
            });
    }

    /// <summary>
    ///     Hides the rows below the specified tree item, leaving each descendant's own expanded state untouched.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose descendants should be hidden.</param>
    /// <remarks>
    ///     Rows are stored in display order, so the descendants are the run of deeper rows right after the item.
    ///     Focus on one of them moves to the item; selected descendants are deselected.
    /// </remarks>
    private void HideChildren(ITreeItem itemAdapter)
    {
        this.LogHideChildrenStarted(itemAdapter);

        var itemIndex = this.shownItems.IndexOf(itemAdapter);
        if (itemIndex == -1)
        {
            // The row is not in the flat shown list at all: a collapsed descendant restored during a search
            // clear, or a second collapse of an already-hidden branch. Its subtree is already out of the list,
            // and the caller records the collapse on the item itself.
            this.LogHideChildrenNotShown(itemAdapter);
            return;
        }

        var first = itemIndex + 1;
        var end = first;
        while (end < this.shownItems.Count && this.shownItems[end].Depth > itemAdapter.Depth)
        {
            end++;
        }

        if (end == first)
        {
            return;
        }

        if (this.FocusedItem?.Item is { } focused)
        {
            for (var index = first; index < end; index++)
            {
                if (ReferenceEquals(this.shownItems[index], focused))
                {
                    this.FocusedItem = new(itemAdapter, RequestOrigin.Programmatic);
                    break;
                }
            }
        }

        this.ReshapeShownItems(
            () =>
            {
                for (var index = end - 1; index >= first; index--)
                {
                    this.LogShownItemsRemoveAt(index);
                    this.shownItems.RemoveAt(index);
                }
            });
    }

    private int FocusedShownIndex()
        => this.FocusedItem is { } focused ? this.shownItems.IndexOf(focused.Item) : -1;

    /// <summary>
    ///     Moves the logical focus off a row that a removal took out of <see cref="ShownItems"/>, onto the row that now
    ///     occupies its place (or the last row), or clears it when nothing is shown.
    /// </summary>
    /// <param name="previousIndex">The shown index of the focused row before the removal, or -1 when it was not shown.</param>
    private void RepairFocusAfterRemoval(int previousIndex)
    {
        if (previousIndex < 0 || this.FocusedItem is not { } focused || this.shownItems.Contains(focused.Item))
        {
            return;
        }

        this.FocusedItem = this.shownItems.Count == 0
            ? null
            : new FocusedItemInfo(this.shownItems[Math.Min(previousIndex, this.shownItems.Count - 1)], focused.Origin);
    }

    private ITreeItem? FindSibling(ITreeItem? parent, bool first)
    {
        var items = this.NavigableItems;
        ITreeItem? target = null;
        for (var index = 0; index < items.Count; index++)
        {
            var item = items[index];
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
