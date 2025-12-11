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
    public Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        this.ValidateItemsAreShown(items);
        var copyableItems = this.FilterClonableItems(items);
        if (copyableItems.Count == 0)
        {
            this.LogCopyIgnoredNoClonableItems(items.Count);
            return Task.CompletedTask;
        }

        this.ClearCutMarks();

        var unique = this.EnsureDistinct(copyableItems);
        this.clipboardItems = unique;
        this.clipboardState = ClipboardState.Copied;
        this.clipboardSourceParent = null;
        this.clipboardIsValid = true;

        this.RaiseClipboardChanged();
        return Task.CompletedTask;
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
    public Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        this.ValidateItemsAreShown(items);
        var eligible = items.Where(item => !item.IsLocked).ToArray();
        if (eligible.Length == 0)
        {
            throw new InvalidOperationException("no cuttable items were provided");
        }

        this.ClearCutMarks();

        foreach (var item in eligible)
        {
            item.IsCut = true;
        }

        this.clipboardItems = eligible;
        this.clipboardState = ClipboardState.Cut;
        this.clipboardSourceParent = eligible[0].Parent;
        this.clipboardIsValid = true;

        this.RaiseClipboardChanged();
        return Task.CompletedTask;
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

        foreach (var item in this.clipboardItems)
        {
            item.IsCut = false;
        }
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

    private ITreeItem[] EnsureDistinct(IReadOnlyCollection<ITreeItem> items)
    {
        _ = this.clipboardState;
        var set = new HashSet<ITreeItem>();
        foreach (var item in items)
        {
            if (!set.Add(item))
            {
                throw new ArgumentException("items contains duplicates", nameof(items));
            }
        }

        return [.. set];
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

            map[original] = customClone.Clone();
        }

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

        foreach (var item in this.clipboardItems)
        {
            item.IsCut = false;
        }

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
