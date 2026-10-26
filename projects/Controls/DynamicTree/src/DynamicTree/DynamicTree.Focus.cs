// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Focus handling of the <see cref="DynamicTree" /> control: following the view model's focused item with platform
///     focus, and reporting platform focus changes back to it.
/// </summary>
public partial class DynamicTree
{
    // A depth, not a flag: a rename's focus and a queued focus operation can overlap, and the first to finish must not
    // re-open the GotFocus feedback loop for the other.
    private int applyingFocusDepth;
    private bool focusOperationPending;

    /// <summary>Overrideable focus hook for every <see cref="ITreeItem"/> implementation.</summary>
    /// <param name="item">The item that received focus.</param>
    /// <returns>True when the tree handled the focus change.</returns>
    protected internal virtual bool OnTreeItemGotFocus(ITreeItem item)
        => item is TreeItemAdapter adapter ? this.OnItemGotFocus(adapter) : this.HandleItemFocus(item);

    /// <summary>
    /// Overrideable hook for when an item receives platform focus.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that received focus.</param>
    protected internal virtual bool OnItemGotFocus(TreeItemAdapter item) => this.HandleItemFocus(item);

    /// <summary>
    ///     When the tree control itself gains focus, move keyboard focus to the selected item if present, otherwise ensure
    ///     there is a focused item.
    /// </summary>
    /// <param name="e">The routed event args.</param>
    protected override void OnGotFocus(RoutedEventArgs e)
    {
        base.OnGotFocus(e);

        // Focus moving between the descendants of the tree reaches here too; only the tree itself gaining focus
        // needs the view model to supply a focused item. Synthetic arguments carry no source.
        if (e.OriginalSource is DependencyObject source && !ReferenceEquals(source, this))
        {
            return;
        }

        if (this.rootGrid is null || this.ViewModel is null
            || this.IsFilterBarElement(e.OriginalSource as DependencyObject))
        {
            return;
        }

        this.LogGotFocus();

        // We want focus visual, so we will request keyboard focus on the focused item.
        _ = this.ViewModel.EnsureFocus(RequestOrigin.KeyboardInput);
    }

    // Releases pending focus work so that nothing runs against an unloaded tree.
    private void ResetFocusState()
    {
        this.focusOperationPending = false;
        this.applyingFocusDepth = 0;
    }

    private bool HandleItemFocus(ITreeItem item)
    {
        // When applying focus through the focus manager, we will get a GotFocus event. We should
        // not propagate that again to the ViewModel as it would create a feedback loop.
        if (this.ViewModel is not { } viewModel || this.applyingFocusDepth > 0)
        {
            return false;
        }

        // Reuse the same request origin as the currently focused item if any; otherwise fall back to programmatic.
        var origin = viewModel.FocusedItem?.Origin ?? RequestOrigin.Programmatic;
        this.LogItemGotFocus(item, origin);
        viewModel.UpdateFocusedItem(item, origin);
        return true;
    }

    private void TreeItem_GotFocus(object sender, RoutedEventArgs args)
    {
        if (sender is not FrameworkElement element || element.DataContext is not ITreeItem item)
        {
            return;
        }

        // When we're applying focus programmatically ignore this GotFocus to avoid a feedback loop.
        // While a queued model->view focus operation is pending, do not process platform GotFocus
        // events that would immediately overwrite the requested model focus. This prevents a race
        // where the platform focuses another item while we are still applying the model's
        // FocusedItem, causing repeated switching.
        if (this.applyingFocusDepth > 0 || this.focusOperationPending)
        {
            return;
        }

        _ = this.OnTreeItemGotFocus(item);
    }

    private void ViewModel_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        if (string.Equals(args.PropertyName, nameof(DynamicTreeViewModel.FilterPredicate), StringComparison.Ordinal))
        {
            this.UpdateDisplayedItems();
            return;
        }

        var prop = args.PropertyName;
        if (!string.IsNullOrEmpty(prop) && !string.Equals(prop, nameof(DynamicTreeViewModel.FocusedItem), StringComparison.Ordinal))
        {
            return;
        }

        if (this.ViewModel.FocusedItem is null)
        {
            this.LogFocusLost();
            return;
        }

        // Ensure focus operations execute on the UI thread. Avoid enqueueing duplicate
        // focus operations if one is already pending.
        if (this.focusOperationPending)
        {
            return;
        }

        this.LogEnqueueApplyFocus();
        this.focusOperationPending = true;

        // The flag is released by the operation itself, or here when it can never run.
        if (this.DispatcherQueue?.TryEnqueue(() => this.RunGuarded(this.ApplyFocusAsync)) != true)
        {
            this.focusOperationPending = false;
        }
    }

    private void ViewModel_OnBringIntoViewRequested(object? sender, DynamicTreeEventArgs args)
    {
        // A row revealed by expanding its ancestors is realized on the next layout pass.
        _ = this.DispatcherQueue.TryEnqueue(
            Microsoft.UI.Dispatching.DispatcherQueuePriority.Low,
            () =>
            {
                if (this.itemsRepeater?.ItemsSourceView?.IndexOf(args.TreeItem) is not int index || index < 0)
                {
                    return;
                }

                (this.itemsRepeater.TryGetElement(index) ?? this.itemsRepeater.GetOrCreateElement(index))?.StartBringIntoView();
            });
    }

    private async Task ApplyFocusAsync()
    {
        try
        {
            // An operation queued before the tree unloaded has nothing to focus.
            if (!this.IsLoaded || this.ViewModel?.FocusedItem is not { } focused)
            {
                return;
            }

            var focusState = focused.Origin switch
            {
                RequestOrigin.KeyboardInput => FocusState.Keyboard,
                RequestOrigin.Programmatic => FocusState.Programmatic,
                RequestOrigin.PointerInput => FocusState.Pointer,
                _ => FocusState.Programmatic,
            };

            this.LogApplyFocus(focused.Item, this.focusOperationPending, focusState);
            await this.FocusRealizedItemAsync(focused.Item, focusState).ConfigureAwait(true);
        }
        finally
        {
            this.focusOperationPending = false;
        }
    }

    /// <summary>
    /// Brings an element into view without animation and completes once the scroll view has moved there: a scroll view
    /// applies the offset on a later frame, after the request returns.
    /// </summary>
    /// <param name="element">The realized element.</param>
    /// <returns>A task that completes when the element is in view.</returns>
    private async Task BringIntoViewAsync(UIElement element)
    {
        if (this.scrollView is not { } scroller)
        {
            element.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
            return;
        }

        int? correlation = null;
        var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnBringingIntoView(ScrollView sender, ScrollingBringingIntoViewEventArgs args) => correlation = args.CorrelationId;
        void OnScrollCompleted(ScrollView sender, ScrollingScrollCompletedEventArgs args)
        {
            if (args.CorrelationId == correlation)
            {
                _ = completed.TrySetResult();
            }
        }

        // An unloaded tree completes no scroll, so unloading ends the wait.
        void OnTreeUnloaded(object sender, RoutedEventArgs args) => _ = completed.TrySetResult();

        scroller.BringingIntoView += OnBringingIntoView;
        scroller.ScrollCompleted += OnScrollCompleted;
        this.Unloaded += OnTreeUnloaded;
        try
        {
            element.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });

            // A request that needs no scroll raises nothing.
            await this.WaitForLayoutAsync().ConfigureAwait(true);
            if (correlation is not null)
            {
                await completed.Task.ConfigureAwait(true);
            }
        }
        finally
        {
            scroller.BringingIntoView -= OnBringingIntoView;
            scroller.ScrollCompleted -= OnScrollCompleted;
            this.Unloaded -= OnTreeUnloaded;
        }
    }

    // Completes after the dispatcher has run the pending layout pass: low-priority work runs after layout.
    private Task WaitForLayoutAsync()
    {
        var settled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (this.DispatcherQueue?.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, settled.SetResult) != true)
        {
            settled.SetResult();
        }

        return settled.Task;
    }

    private async Task FocusRealizedItemAsync(ITreeItem item, FocusState focusState)
    {
        if (item is null || this.itemsRepeater is null || this.ViewModel is null)
        {
            return;
        }

        // IMPORTANT: the ItemsRepeater may be bound either to ShownItems or to FilteredItems.
        // Always resolve the index against the repeater's *current* source, otherwise focus
        // will target the wrong element whenever filtering is enabled.
        var index = this.itemsRepeater.ItemsSourceView?.IndexOf(item) ?? this.ViewModel.ShownIndexOf(item);
        if (index == -1)
        {
            this.LogFocusIndexMissing(item);
            return;
        }

        var element = this.itemsRepeater.TryGetElement(index);
        if (element is null)
        {
            // A row the repeater creates on request has no position until it is arranged.
            element = this.itemsRepeater.GetOrCreateElement(index);
            element?.UpdateLayout();
        }

        if (element is null)
        {
            this.LogFocusElementMissing(item, index);
            return;
        }

        // Key finding: calling StartBringIntoView as part of a focus change can cause an
        // *outer* scroller (outside this control) to take focus with FocusState.PointerInput
        // on mouse release, which can then raise GotFocus events while we're still
        // processing the focus request. To avoid processing those intermediate GotFocus
        // events we mark that we're applying focus before we start the BringIntoView
        // operation so TreeItem_GotFocus ignores them.
        this.applyingFocusDepth++;
        try
        {
            // A pointer focuses the row it pressed, which is already in view; every other focus change, such as an
            // arrow key, a page jump or a type-ahead match, can target a row outside the viewport.
            if (focusState != FocusState.Pointer)
            {
                element.StartBringIntoView();
            }

            var focusTarget = this.GetRow(element) as DependencyObject ?? element;

            if (focusTarget is Control control)
            {
                control.IsTabStop = true;
                control.UseSystemFocusVisuals = focusState == FocusState.Keyboard;
            }

            this.LogFocusApplyAttempt(item, index, focusState, this.focusOperationPending);
            var result = await FocusManager.TryFocusAsync(focusTarget, focusState);
            this.LogFocusResult(item, index, focusTarget, result.Succeeded);
        }
        finally
        {
            // The tree may have unloaded and reset the depth while focus was applied.
            this.applyingFocusDepth = Math.Max(0, this.applyingFocusDepth - 1);
        }
    }
}
