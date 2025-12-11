// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Controls;

/// <summary>
///     Clipboard management for <see cref="DynamicTreeViewModel" />.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    private ITreeItem[] clipboardItems = [];
    private ITreeItem[] cutMarkedItems = [];
    private ClipboardState clipboardState = ClipboardState.Empty;
    private ITreeItem? clipboardSourceParent;
    private bool clipboardIsValid = true;

    /// <summary>
    ///     Gets the current clipboard state.
    /// </summary>
    public ClipboardState CurrentClipboardState => this.clipboardState;

    /// <summary>
    ///     Gets the items currently stored in the clipboard, or an empty list when the state is <see cref="ClipboardState.Empty" />.
    /// </summary>
    public IReadOnlyList<ITreeItem> ClipboardItems => this.clipboardState == ClipboardState.Empty ? [] : this.clipboardItems;

    /// <summary>
    ///     Gets a value indicating whether the clipboard contents are still valid in the current tree.
    /// </summary>
    public bool IsClipboardValid => this.clipboardIsValid;

    /// <summary>
    ///     Copies the provided items into the clipboard. Items must be visible in the tree.
    /// </summary>
    /// <param name="items">The items to copy.</param>
    /// <returns>A completed task.</returns>
    public async Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        this.ValidateItemsAreShown(items);

        // Expand selection to include all descendants so copying a node copies its subtree.
        var expanded = await ExpandSelectionWithDescendantsAsync(items).ConfigureAwait(true);
        var copyableItems = this.FilterClonableItems(expanded);
        if (copyableItems.Count == 0)
        {
            this.LogCopyIgnoredNoClonableItems(items.Count);
            return;
        }

        this.ClearCutMarks();

        var unique = this.EnsureDistinct(copyableItems);
        this.clipboardItems = unique;
        this.clipboardState = ClipboardState.Copied;
        this.clipboardSourceParent = null;
        this.clipboardIsValid = true;

        this.RaiseClipboardChanged();
        return;
    }

    /// <summary>
    ///     Copies the provided item into the clipboard. Item must be visible in the tree.
    /// </summary>
    /// <param name="item">The item to copy.</param>
    /// <returns>A completed task.</returns>
    public Task CopyItemAsync(ITreeItem item) => this.CopyItemsAsync([item]);

    /// <summary>
    ///     Cuts the provided items into the clipboard, marking them as cut. Locked items are skipped.
    /// </summary>
    /// <param name="items">The items to cut.</param>
    /// <returns>A completed task.</returns>
    public async Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
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

        var eligible = topLevel;
        if (eligible.Length == 0)
        {
            throw new InvalidOperationException("no cuttable items were provided");
        }

        this.ClearCutMarks();

        foreach (var item in markList)
        {
            item.IsCut = true;
        }

        this.clipboardItems = eligible;
        this.cutMarkedItems = [.. markList];
        this.clipboardState = ClipboardState.Cut;
        this.clipboardSourceParent = eligible[0].Parent;
        this.clipboardIsValid = true;

        this.RaiseClipboardChanged();
        return;
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
        this.ClearCutMarks();
        this.clipboardItems = [];
        this.clipboardState = ClipboardState.Empty;
        this.clipboardSourceParent = null;
        this.clipboardIsValid = true;
        this.RaiseClipboardChanged();
        return Task.CompletedTask;
    }

    /// <summary>
    ///     Pastes the clipboard items into the specified parent or the currently focused item when none is provided.
    /// </summary>
    /// <param name="targetParent">Optional target parent. When <see langword="null" />, the focused item is used.</param>
    /// <param name="insertIndex">Optional insertion index under the target parent.</param>
    /// <returns>A task that completes when paste is finished.</returns>
    public async Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null)
    {
        if (this.clipboardState == ClipboardState.Empty)
        {
            throw new InvalidOperationException("clipboard is empty");
        }

        if (!this.clipboardIsValid)
        {
            throw new InvalidOperationException("Clipboard items were deleted from tree");
        }

        if (this.FocusedItem is null || this.SelectionModel?.IsEmpty != false)
        {
            throw new InvalidOperationException("paste requires a focused and selected item");
        }

        var parent = targetParent ?? this.FocusedItem
            ?? throw new InvalidOperationException("cannot paste without a target parent");

        if (!this.ShownItems.Contains(parent))
        {
            throw new InvalidOperationException("target parent must be shown");
        }

        var targetIndex = insertIndex ?? parent.ChildrenCount;
        if (this.clipboardState == ClipboardState.Cut && !this.IsValidPasteTarget(parent))
        {
            return;
        }

        var pastedItems = this.clipboardState switch
        {
            ClipboardState.Copied => await this.PasteCopiedItemsAsync(parent, targetIndex).ConfigureAwait(true),
            ClipboardState.Cut => await this.PasteCutItemsAsync(parent, targetIndex).ConfigureAwait(true),
            _ => throw new InvalidOperationException("unsupported clipboard state"),
        };

        this.clipboardItems = [];
        this.clipboardState = ClipboardState.Empty;
        this.clipboardSourceParent = null;
        this.clipboardIsValid = true;
        this.RaiseClipboardChanged();

        this.SelectionModel?.ClearSelection();
        foreach (var item in pastedItems)
        {
            this.SelectItem(item);
        }

        _ = this.FocusItem(pastedItems.FirstOrDefault());
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

            if (!isDescendant && !result.Contains(item))
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

    private bool IsValidPasteTarget(ITreeItem targetParent)
    {
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

    private void RaiseClipboardChanged()
        => this.ClipboardContentChanged?.Invoke(
            this,
            new ClipboardContentChangedEventArgs
            {
                NewState = this.clipboardState,
                Items = this.ClipboardItems,
                IsValid = this.clipboardIsValid,
            });

    private void ClearCutMarks()
    {
        if (this.clipboardState != ClipboardState.Cut)
        {
            return;
        }

        foreach (var item in this.cutMarkedItems)
        {
            item.IsCut = false;
        }

        this.cutMarkedItems = [];
    }

    private ITreeItem[] EnsureDistinct(IReadOnlyCollection<ITreeItem> items)
    {
        _ = this.clipboardState;
        var set = new HashSet<ITreeItem>();
        var result = new List<ITreeItem>();
        foreach (var item in items)
        {
            if (set.Add(item))
            {
                result.Add(item);
            }
        }

        return [.. result];
    }

    private void ValidateItemsAreShown(IEnumerable<ITreeItem> items)
    {
        foreach (var item in items)
        {
            if (!this.ShownItems.Contains(item))
            {
                throw new InvalidOperationException("item must be shown to copy or cut");
            }
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "keep read-only")]
    private IReadOnlyList<ITreeItem> FilterClonableItems(IEnumerable<ITreeItem> items)
    {
        var copyable = new List<ITreeItem>();
        foreach (var item in items)
        {
            if (item is ICanBeCloned)
            {
                copyable.Add(item);
            }
            else
            {
                this.LogCopySkippedNonClonableItem(item);
            }
        }

        return copyable;
    }

    private async Task<List<ITreeItem>> PasteCopiedItemsAsync(ITreeItem targetParent, int insertIndex)
    {
        var clones = await this.CloneClipboardItemsAsync().ConfigureAwait(true);
        var nextIndex = insertIndex;
        foreach (var clone in clones)
        {
            await this.InsertItemAsync(nextIndex, targetParent, clone).ConfigureAwait(true);
            nextIndex++;
            if (clone.IsExpanded)
            {
                await this.RestoreExpandedChildrenAsync(clone).ConfigureAwait(true);
            }
        }

        return clones;
    }

    private async Task<List<ITreeItem>> CloneClipboardItemsAsync()
    {
        var map = new Dictionary<ITreeItem, ITreeItem>(this.clipboardItems.Length);
        foreach (var original in this.clipboardItems)
        {
            if (original is not ICanBeCloned customClone)
            {
                throw new InvalidOperationException($"type '{original.GetType()}' must implement {nameof(ICanBeCloned)} to support copy/paste");
            }

            map[original] = customClone.CloneSelf();
        }

#if DEBUG
        // Sanity check: produced clones should not include children. If this assertion fires, a custom
        // `ICanBeCloned.CloneSelf()` implementation created a deep clone; adapting clones to be orphaned is
        // the responsibility of the `CloneSelf()` implementor, not the clipboard code.
        foreach (var clone in map.Values)
        {
            if (clone is TreeItemAdapter adapter)
            {
                var children = await adapter.Children.ConfigureAwait(true);
                Debug.Assert(
                    children.Count == 0,
                    "ICanBeCloned.Clone() returned a clone with pre-attached children; clones must not include children.");
            }
        }
#endif // DEBUG

        foreach (var original in this.clipboardItems)
        {
            if (original.Parent is not null && map.TryGetValue(original.Parent, out var parentClone))
            {
                if (parentClone is TreeItemAdapter adapter)
                {
                    await adapter.AddChildAsync(map[original]).ConfigureAwait(true);
                }
                else
                {
                    throw new InvalidOperationException("parent clone does not support children");
                }
            }
        }

        return [.. map.Where(kvp => kvp.Key.Parent is null || !map.ContainsKey(kvp.Key.Parent)).Select(kvp => kvp.Value)];
    }

    private async Task<List<ITreeItem>> PasteCutItemsAsync(ITreeItem targetParent, int insertIndex)
    {
        this.EnsureNoCycles(targetParent);
        await this.MoveItemsAsync(this.clipboardItems, targetParent, insertIndex).ConfigureAwait(true);

        // Clear cut visual flags for all marked items (top-level parents + descendants)
        foreach (var item in this.cutMarkedItems)
        {
            item.IsCut = false;
        }

        this.cutMarkedItems = [];

        return [.. this.clipboardItems];
    }

    private void EnsureNoCycles(ITreeItem targetParent)
    {
        foreach (var item in this.clipboardItems)
        {
            if (ReferenceEquals(item, targetParent))
            {
                throw new InvalidOperationException("cannot paste into source item");
            }

            var current = targetParent.Parent;
            while (current is not null)
            {
                if (ReferenceEquals(current, item))
                {
                    throw new InvalidOperationException("cannot paste into a descendant of the source item");
                }

                current = current.Parent;
            }
        }
    }
}
