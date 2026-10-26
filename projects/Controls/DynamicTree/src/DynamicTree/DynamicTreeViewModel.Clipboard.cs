// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Clipboard management for <see cref="DynamicTreeViewModel" />.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    private ITreeItem[] clipboardItems = [];
    private ITreeItem[] cutMarkedItems = [];
    private ClipboardState clipboardState = ClipboardState.Empty;
    private bool clipboardIsValid = true;
    private bool isPastingCutItems;

    /// <summary>
    ///     Gets the current clipboard state.
    /// </summary>
    public ClipboardState CurrentClipboardState => this.clipboardState;

    /// <summary>
    ///     Gets the items currently stored in the clipboard, or an empty list when the state is <see cref="ClipboardState.Empty" />.
    /// </summary>
    public IReadOnlyList<ITreeItem> ClipboardItems => this.clipboardState == ClipboardState.Empty ? [] : Array.AsReadOnly(this.clipboardItems);

    /// <summary>
    ///     Gets a value indicating whether the clipboard contents are still valid in the current tree.
    /// </summary>
    public bool IsClipboardValid => this.clipboardIsValid;

    /// <summary>
    ///     Copies the provided items into the clipboard. Items must be visible in the tree.
    /// </summary>
    /// <param name="items">The items to copy.</param>
    /// <returns>A completed task.</returns>
    public virtual async Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        this.ValidateItemsAreShown(items);

        // Copying a node copies its subtree. Expanding from the top-level roots lists each subtree depth-first, so the
        // clones get their children back in the original order whatever the order of the selection.
        var expanded = await ExpandSelectionWithDescendantsAsync(ExtractTopLevelSelection(items)).ConfigureAwait(true);
        var copyableItems = this.FilterClonableSubtrees(expanded);
        if (copyableItems.Count == 0)
        {
            this.LogCopyIgnoredNoClonableItems(items.Count);
            return;
        }

        this.SetClipboard(ClipboardState.Copied, [.. copyableItems], cutMarks: []);
    }

    /// <summary>
    ///     Copies the provided item into the clipboard. Item must be visible in the tree.
    /// </summary>
    /// <param name="item">The item to copy.</param>
    /// <returns>A completed task.</returns>
    public Task CopyItemAsync(ITreeItem item) => this.CopyItemsAsync([item]);

    /// <summary>Duplicates shown items at a destination without changing the user's clipboard.</summary>
    /// <param name="items">The source roots to duplicate.</param>
    /// <param name="targetParent">The destination parent.</param>
    /// <param name="insertIndex">The destination child insertion index.</param>
    /// <returns>The newly inserted roots.</returns>
    public async Task<IReadOnlyList<ITreeItem>> DuplicateItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem targetParent, int insertIndex)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(targetParent);
        if (items.Count == 0)
        {
            return [];
        }

        this.ValidateItemsAreShown(items);
        if (!this.CanShowChildrenOf(targetParent) || !CanReceiveChildren(targetParent))
        {
            return [];
        }

        var roots = ExtractTopLevelSelection(items);
        if (roots.Any(root => root.IsLocked))
        {
            return [];
        }

        var originals = await ExpandSelectionWithDescendantsAsync(roots).ConfigureAwait(true);
        if (originals.Any(static original => original is not ICanBeCloned))
        {
            return [];
        }

        var rootClones = await this.InsertClonesAsync(roots, originals, targetParent, insertIndex).ConfigureAwait(true);
        if (rootClones.Count > 0)
        {
            this.SelectInsertedItems(rootClones);
        }

        return rootClones;
    }

    /// <summary>
    ///     Cuts the provided items into the clipboard, marking them as cut. Locked items are skipped.
    /// </summary>
    /// <param name="items">The items to cut.</param>
    /// <returns>A completed task.</returns>
    public virtual async Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        this.ValidateItemsAreShown(items);
        var eligibleInitial = items.Where(item => !item.IsLocked).ToArray();
        if (eligibleInitial.Length == 0)
        {
            throw new InvalidOperationException("no cuttable items were provided");
        }

        // Determine top-level items to cut (skip descendants of already selected items), and mark all descendants for visual cut state.
        var topLevel = ExtractTopLevelSelection(eligibleInitial).ToArray();
        var markList = new List<ITreeItem>();
        foreach (var item in topLevel)
        {
            markList.Add(item);
            markList.AddRange(await CollectDescendantsAsync(item).ConfigureAwait(true));
        }

        this.SetClipboard(ClipboardState.Cut, topLevel, [.. markList]);
    }

    /// <summary>
    ///     Cuts the provided item into the clipboard. Item must be visible in the tree.
    /// </summary>
    /// <param name="item">The item to cut.</param>
    /// <returns>A completed task.</returns>
    public Task CutItemAsync(ITreeItem item) => this.CutItemsAsync([item]);

    /// <summary>
    ///     Clears the clipboard and removes any cut markings.
    /// </summary>
    /// <returns>A completed task.</returns>
    public Task ClearClipboardAsync()
    {
        this.ClearClipboard();
        return Task.CompletedTask;
    }

    /// <summary>
    ///     Pastes the clipboard items into the specified parent or the currently focused item when none is provided.
    /// </summary>
    /// <param name="targetParent">Optional target parent. When <see langword="null" />, the focused item is used.</param>
    /// <param name="insertIndex">Optional insertion index under the target parent.</param>
    /// <returns>A task that completes when paste is finished.</returns>
    public virtual async Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null)
    {
        if (this.clipboardState == ClipboardState.Empty)
        {
            throw new InvalidOperationException("clipboard is empty");
        }

        if (!this.clipboardIsValid)
        {
            throw new InvalidOperationException("Clipboard items were deleted from tree");
        }

        // Without an explicit target the focused item receives the paste, as documented.
        var parent = targetParent ?? this.FocusedItem?.Item
            ?? throw new InvalidOperationException("paste requires a target parent or a focused item");

        if (!this.CanShowChildrenOf(parent))
        {
            throw new InvalidOperationException("target parent must be shown");
        }

        if (!this.IsValidPasteTarget(parent))
        {
            return;
        }

        var targetIndex = insertIndex ?? parent.ChildrenCount;
        var pastedItems = this.clipboardState switch
        {
            ClipboardState.Copied => await this.PasteCopiedItemsAsync(parent, targetIndex).ConfigureAwait(true),
            ClipboardState.Cut => await this.PasteCutItemsAsync(parent, targetIndex).ConfigureAwait(true),
            _ => throw new InvalidOperationException("unsupported clipboard state"),
        };

        if (pastedItems.Count == 0)
        {
            // A vetoed move or insertion pastes nothing; the clipboard stays as it was.
            return;
        }

        // A copy can be pasted again until the next copy or cut replaces it. A cut is a staged move: once the items
        // moved, nothing is left to paste, so the clipboard empties.
        if (this.clipboardState == ClipboardState.Cut)
        {
            this.ClearClipboard();
        }

        this.SelectInsertedItems(pastedItems);
    }

    /// <summary>
    ///     Invoked after the base clipboard is cleared so derived types can clear their own
    ///     clipboard state (for example, staged node identities).
    /// </summary>
    protected virtual void OnClipboardCleared()
    {
    }

    /// <summary>
    ///     Stages rows whose payload a derived view model keeps on its own domain clipboard, so the tree reports the
    ///     clipboard state and dims cut rows like its own copy and cut do.
    /// </summary>
    /// <param name="items">The staged rows.</param>
    /// <param name="cut"><see langword="true" /> to stage a cut and mark the rows as cut; otherwise a copy.</param>
    protected void StageClipboard(IReadOnlyList<ITreeItem> items, bool cut)
    {
        ArgumentNullException.ThrowIfNull(items);
        this.SetClipboard(cut ? ClipboardState.Cut : ClipboardState.Copied, [.. items], cut ? [.. items] : []);
    }

    /// <summary>
    ///     Releases the staged rows and their cut marks while the clipboard state stays, for a derived view model whose
    ///     domain payload outlives the rows that showed it (for example, when another document replaces them).
    /// </summary>
    protected void ReleaseClipboardRows()
    {
        if (this.clipboardItems.Length == 0 && this.cutMarkedItems.Length == 0)
        {
            return;
        }

        this.ClearCutMarks();
        this.clipboardItems = [];
        this.RaiseClipboardChanged();
    }

    /// <summary>Empties the clipboard, removes any cut marks and lets a derived type clear its own payload.</summary>
    protected void ClearClipboard()
    {
        this.ClearCutMarks();
        this.clipboardItems = [];
        this.clipboardState = ClipboardState.Empty;
        this.clipboardIsValid = true;
        this.OnClipboardCleared();
        this.RaiseClipboardChanged();
    }

    private static async Task<List<ITreeItem>> CollectDescendantsAsync(ITreeItem node)
    {
        var list = new List<ITreeItem>();
        var children = await node.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            list.Add(child);
            list.AddRange(await CollectDescendantsAsync(child).ConfigureAwait(true));
        }

        return list;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "read-only is more important than this optimization")]
    private static IReadOnlyList<ITreeItem> ExtractTopLevelSelection(IReadOnlyCollection<ITreeItem> items)
    {
        var set = new HashSet<ITreeItem>(items);
        var added = new HashSet<ITreeItem>();
        var result = new List<ITreeItem>(items.Count);

        foreach (var item in items)
        {
            var isDescendant = false;
            var p = item.Parent;
            while (p is not null)
            {
                if (set.Contains(p))
                {
                    isDescendant = true;
                    break;
                }

                p = p.Parent;
            }

            if (!isDescendant && added.Add(item))
            {
                result.Add(item);
            }
        }

        return result;
    }

    private static async Task<IReadOnlyList<ITreeItem>> ExpandSelectionWithDescendantsAsync(IEnumerable<ITreeItem> items)
    {
        var unique = new List<ITreeItem>();
        var seen = new HashSet<ITreeItem>();

        foreach (var item in items)
        {
            if (seen.Add(item))
            {
                unique.Add(item);
            }

            foreach (var desc in await CollectDescendantsAsync(item).ConfigureAwait(true))
            {
                if (seen.Add(desc))
                {
                    unique.Add(desc);
                }
            }
        }

        return unique;
    }

    private static async Task<Dictionary<ITreeItem, ITreeItem>> CloneSubtreesAsync(IReadOnlyList<ITreeItem> originals)
    {
        var clones = new Dictionary<ITreeItem, ITreeItem>();
        foreach (var original in originals)
        {
            clones.Add(original, ((ICanBeCloned)original).CloneSelf());
        }

        foreach (var original in originals)
        {
            if (original.Parent is { } parent && clones.TryGetValue(parent, out var parentClone))
            {
                await parentClone.AddChildAsync(clones[original]).ConfigureAwait(true);
            }
        }

        return clones;
    }

    private static bool CanReceiveChildren(ITreeItem item) => item.CanAcceptChildren && (!item.IsLocked || item.IsRoot);

    // A hidden root is not a row, but its children are the top-level rows.
    private bool CanShowChildrenOf(ITreeItem parent) => parent.IsRoot || this.IsShown(parent);

    private bool IsValidPasteTarget(ITreeItem targetParent)
    {
        if (!CanReceiveChildren(targetParent))
        {
            this.LogPasteSkippedTargetRejectsChildren(targetParent);
            return false;
        }

        // Copies never move a source, so only a cut can paste into itself or one of its descendants.
        if (this.clipboardState != ClipboardState.Cut)
        {
            return true;
        }

        foreach (var item in this.clipboardItems)
        {
            if (ReferenceEquals(item, targetParent))
            {
                this.LogPasteSkippedIntoSource(item);
                return false;
            }

            var current = targetParent.Parent;
            while (current is not null)
            {
                if (ReferenceEquals(current, item))
                {
                    this.LogPasteSkippedIntoDescendant(item, targetParent);
                    return false;
                }

                current = current.Parent;
            }
        }

        return true;
    }

    /// <summary>
    ///     Replaces the selection with freshly inserted items as one selection transaction: the first item that the
    ///     selection mode and co-selection policy accept becomes the active item and the focus, and
    ///     <see cref="SelectionSettled" /> is raised once.
    /// </summary>
    /// <param name="items">The inserted or moved items.</param>
    private void SelectInsertedItems(IReadOnlyList<ITreeItem> items)
    {
        this.WithSelectionBatch(() =>
        {
            this.SetActiveItem(item: null);
            if (this.SelectionModel is not { } selection)
            {
                return;
            }

            selection.ClearSelection();

            var chosen = new List<ITreeItem>(items.Count);
            foreach (var item in items)
            {
                if (!this.shownItems.Contains(item))
                {
                    continue;
                }

                if (chosen.Count == 0 || (this.SelectionMode == SelectionMode.Multiple && this.AllowsCoSelection(item, chosen)))
                {
                    chosen.Add(item);
                }
            }

            if (chosen.Count == 0)
            {
                return;
            }

            foreach (var item in chosen)
            {
                selection.SelectItem(item);
            }

            this.SetActiveItem(chosen[0]);
            _ = this.FocusItem(chosen[0], RequestOrigin.Programmatic);
        });
    }

    private void InvalidateClipboardDueToMutation()
    {
        if (this.clipboardState == ClipboardState.Empty || !this.clipboardIsValid)
        {
            return;
        }

        this.ClearCutMarks();
        this.clipboardIsValid = false;
        this.RaiseClipboardChanged();
    }

    private void InvalidateClipboardIfSourcesAffected(IEnumerable<ITreeItem> affectedItems)
    {
        // Pasting a cut moves its own sources: that is the clipboard being consumed, not mutated.
        if (this.isPastingCutItems || this.clipboardState == ClipboardState.Empty || !this.clipboardIsValid)
        {
            return;
        }

        var affected = affectedItems.ToHashSet();
        foreach (var source in this.clipboardItems)
        {
            for (var current = source; current is not null; current = current.Parent)
            {
                if (affected.Contains(current))
                {
                    this.InvalidateClipboardDueToMutation();
                    return;
                }
            }
        }
    }

    private void SetClipboard(ClipboardState state, ITreeItem[] items, ITreeItem[] cutMarks)
    {
        this.ClearCutMarks();

        foreach (var item in cutMarks)
        {
            item.IsCut = true;
        }

        this.clipboardItems = items;
        this.cutMarkedItems = cutMarks;
        this.clipboardState = state;
        this.clipboardIsValid = true;

        this.RaiseClipboardChanged();
    }

    private void RaiseClipboardChanged()
        => this.ClipboardContentChanged?.Invoke(
            this,
            new ClipboardContentChangedEventArgs
            {
                NewState = this.clipboardState,
                Items = this.ClipboardItems,
                IsValid = this.clipboardIsValid,
            });

    // Marks are tracked on their own, not by state, so a cut whose state changed first still loses its marks.
    private void ClearCutMarks()
    {
        foreach (var item in this.cutMarkedItems)
        {
            item.IsCut = false;
        }

        this.cutMarkedItems = [];
    }

    private void ValidateItemsAreShown(IEnumerable<ITreeItem> items)
    {
        foreach (var item in items)
        {
            if (!this.IsShown(item))
            {
                throw new InvalidOperationException("item must be shown to copy or cut");
            }
        }
    }

    /// <summary>
    ///     Keeps the clonable items whose copied ancestors are all clonable: a non-clonable item drops its whole
    ///     copied subtree, since its descendants would otherwise paste as unrelated extra roots.
    /// </summary>
    /// <param name="items">The copied items, including descendants.</param>
    /// <returns>The items to place in the clipboard.</returns>
    private List<ITreeItem> FilterClonableSubtrees(IReadOnlyList<ITreeItem> items)
    {
        var skipped = new HashSet<ITreeItem>();
        foreach (var item in items)
        {
            if (item is not ICanBeCloned)
            {
                _ = skipped.Add(item);
                this.LogCopySkippedNonClonableItem(item);
            }
        }

        var copyable = new List<ITreeItem>(items.Count - skipped.Count);
        foreach (var item in items)
        {
            if (skipped.Contains(item))
            {
                continue;
            }

            var hasSkippedAncestor = false;
            for (var ancestor = item.Parent; ancestor is not null && !hasSkippedAncestor; ancestor = ancestor.Parent)
            {
                hasSkippedAncestor = skipped.Contains(ancestor);
            }

            if (!hasSkippedAncestor)
            {
                copyable.Add(item);
            }
        }

        return copyable;
    }

    // The clipboard holds each copied root followed by its descendants, so its top-level items are the roots to paste.
    private Task<IReadOnlyList<ITreeItem>> PasteCopiedItemsAsync(ITreeItem targetParent, int insertIndex)
        => this.InsertClonesAsync(ExtractTopLevelSelection(this.clipboardItems), this.clipboardItems, targetParent, insertIndex);

    /// <summary>
    ///     Clones the subtrees of <paramref name="roots" /> and inserts the root clones as one transaction: the owner
    ///     approves every root first, and a later refusal rolls back the clones already inserted.
    /// </summary>
    /// <param name="roots">The source roots, in insertion order.</param>
    /// <param name="originals">The roots and their descendants, each parent listed before its children.</param>
    /// <param name="targetParent">The destination parent.</param>
    /// <param name="insertIndex">The destination child insertion index.</param>
    /// <returns>The inserted root clones, or an empty list when the insertion was refused.</returns>
    private async Task<IReadOnlyList<ITreeItem>> InsertClonesAsync(IReadOnlyList<ITreeItem> roots, IReadOnlyList<ITreeItem> originals, ITreeItem targetParent, int insertIndex)
    {
        var clones = await CloneSubtreesAsync(originals).ConfigureAwait(true);
        var rootClones = roots.Select(root => clones[root]).ToArray();
        if (!await this.DisplayHelper.InsertItemsAsync(rootClones, targetParent, insertIndex, this.InsertItemAsync).ConfigureAwait(true))
        {
            return [];
        }

        foreach (var clone in rootClones.Where(static item => item.IsExpanded))
        {
            await this.RestoreExpandedChildrenAsync(clone).ConfigureAwait(true);
        }

        return Array.AsReadOnly(rootClones);
    }

    private async Task<IReadOnlyList<ITreeItem>> PasteCutItemsAsync(ITreeItem targetParent, int insertIndex)
    {
        var moved = false;
        void OnMoved(object? sender, TreeItemsMovedEventArgs args) => moved = true;

        this.isPastingCutItems = true;
        this.ItemMoved += OnMoved;
        try
        {
            await this.MoveItemsAsync(this.clipboardItems, targetParent, insertIndex).ConfigureAwait(true);
        }
        finally
        {
            this.ItemMoved -= OnMoved;
            this.isPastingCutItems = false;
        }

        // A handler that vetoed the move changed nothing, so the cut stays staged.
        return moved ? Array.AsReadOnly(this.clipboardItems) : [];
    }
}
