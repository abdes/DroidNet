// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Keyboard command routing of the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    /// <summary>
    ///     Handles keyboard input for the tree control.
    /// </summary>
    /// <param name="key">The virtual key that was pressed.</param>
    /// <param name="isControlDown">True when the Control key is held down during the key event.</param>
    /// <param name="isShiftDown">True when the Shift key is held down during the key event.</param>
    /// <returns>
    ///     A task that completes with <see langword="true"/> when the key was handled by the control,
    ///     otherwise <see langword="false"/>.
    /// </returns>
    /// <remarks>
    ///     This method implements the control's keyboard command routing and is designed to be
    ///     invoked both from platform key event handlers (which compute modifier state from the
    ///     platform) and directly from tests. It delegates to various ViewModel operations for
    ///     navigation, selection and clipboard shortcuts.
    /// </remarks>
    [SuppressMessage("Design", "MA0051:Method is too long", Justification = "code is clearer with all keys in the sme place")]
    protected internal virtual async Task<bool> HandleKeyDownAsync(VirtualKey key, bool isControlDown, bool isShiftDown)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        switch (key)
        {
            case VirtualKey.A when isControlDown:
                if (this.SelectionScope == TreeSelectionScope.DisplayedItems)
                {
                    this.ViewModel.ToggleDisplayedSelection(this.GetDisplayedItemsSnapshot());
                }
                else
                {
                    this.ViewModel.ToggleSelectAllCommand.Execute(parameter: null);
                }

                return true;

            case VirtualKey.I when isControlDown && isShiftDown:
                if (this.SelectionScope == TreeSelectionScope.DisplayedItems)
                {
                    this.ViewModel.InvertDisplayedSelection(this.GetDisplayedItemsSnapshot());
                }
                else
                {
                    this.ViewModel.InvertSelectionCommand.Execute(parameter: null);
                }

                return true;

            case VirtualKey.Up:
                return this.MoveFocus(FocusNavigationDirection.Up);

            case VirtualKey.Down:
                return this.MoveFocus(FocusNavigationDirection.Down);

            case VirtualKey.PageUp:
                return this.MoveFocusByPage(forward: false);

            case VirtualKey.PageDown:
                return this.MoveFocusByPage(forward: true);

            case VirtualKey.Left:
                // Record this as a keyboard-origin focus change so the view uses keyboard focus visuals. The
                // ViewModel reasserts FocusedItem; the view performs the actual keyboard focus.
                return await this.ViewModel.CollapseFocusedItemAsync().ConfigureAwait(true);

            case VirtualKey.Right:
                return await this.ViewModel.ExpandFocusedItemAsync().ConfigureAwait(true);

            case VirtualKey.Home:
                return isControlDown
                    ? this.ViewModel.FocusFirstVisibleItemInTree(RequestOrigin.KeyboardInput)
                    : this.ViewModel.FocusFirstVisibleItemInParent(RequestOrigin.KeyboardInput);

            case VirtualKey.End:
                return isControlDown
                    ? this.ViewModel.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput)
                    : this.ViewModel.FocusLastVisibleItemInParent(RequestOrigin.KeyboardInput);

            case VirtualKey.Enter:
            case VirtualKey.Space:
                if (this.ViewModel.FocusedItem is { Item: { } item })
                {
                    if (item.IsSelected)
                    {
                        this.ViewModel.ClearSelectionCommand.Execute(item);
                    }
                    else
                    {
                        this.SelectItem(item, isControlDown, isShiftDown, RequestOrigin.KeyboardInput);
                    }

                    return true;
                }

                return false;

            case VirtualKey.Escape:
                // An in-flight internal drag outranks every other Escape meaning: dropping the
                // drag state here makes the pending Drop event find no sources and report None, so
                // the tree is left exactly as it was before the drag started.
                if (this.draggedItems is not null)
                {
                    this.ClearDragState();
                    return true;
                }

                if (this.TryHandleTypeAhead(VirtualKey.Escape))
                {
                    return true;
                }

                // Nothing staged to cancel means Escape had nothing to do, and reporting it as
                // handled would swallow the key from every outer handler (rename box, dialog,
                // navigation). Only consume it when there is a cut/copy staging to drop.
                if (this.ViewModel.CurrentClipboardState != ClipboardState.Empty)
                {
                    await this.ViewModel.ClearClipboardAsync().ConfigureAwait(true);
                    return true;
                }

                return false;
        }

        // Alt combinations are access keys and menu accelerators, never type-ahead.
        return !isControlDown && !IsMenuKeyDown() && this.TryHandleTypeAhead(key);
    }

    // Keys whose command may complete only after an await, so the key is owned before its outcome is known.
    private static bool MayCompleteAsynchronously(VirtualKey key)
        => key is VirtualKey.Left or VirtualKey.Right or VirtualKey.Escape;

    private void ItemsRepeater_OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        this.LogKeyDown(args.Key);
        if (IsInteractiveSource(args.OriginalSource as DependencyObject))
        {
            return;
        }

        this.HandleKeyDown(args);

        // Always swallow ItemsRepeater's default handling for paging keys to avoid layout nudges.
        if (!args.Handled && args.Key is VirtualKey.PageUp or VirtualKey.PageDown or VirtualKey.Home or VirtualKey.End)
        {
            args.Handled = true;
        }
    }

    // The outcome is decided synchronously whenever the command completes synchronously. A command that has to wait
    // owns its key from the start, and its failure is logged rather than lost.
    private void HandleKeyDown(KeyRoutedEventArgs args)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        var command = this.HandleKeyDownAsync(args.Key, IsControlKeyDown(), IsShiftKeyDown());
        if (command.IsCompletedSuccessfully)
        {
            // The handler sees handled events too: a declined key keeps whatever handled it before.
            args.Handled |= command.Result;
            return;
        }

        args.Handled |= MayCompleteAsynchronously(args.Key);
        this.RunGuarded(() => command);
    }

    private bool MoveFocus(FocusNavigationDirection direction)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // The ViewModel changes FocusedItem; the view applies platform focus in its property-changed handler.
        return direction switch
        {
            FocusNavigationDirection.Down => this.ViewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput),
            FocusNavigationDirection.Up => this.ViewModel.FocusPreviousVisibleItem(RequestOrigin.KeyboardInput),
            _ => false,
        };
    }

    // Moves focus by the rows a viewport shows, less one so that the previous page's edge row stays in view.
    private bool MoveFocusByPage(bool forward)
    {
        if (this.ViewModel?.FocusedItem is not { } focused || this.itemsRepeater?.ItemsSourceView is not { Count: > 0 } source)
        {
            return false;
        }

        var current = source.IndexOf(focused.Item);
        if (current < 0)
        {
            return false;
        }

        var pageRows = Math.Max(1, (int)((this.scrollView?.ViewportHeight ?? 0) / Math.Max(this.ItemRowHeight, 1)) - 1);
        var target = Math.Clamp(current + (forward ? pageRows : -pageRows), 0, source.Count - 1);
        return target != current
            && source.GetAt(target) is ITreeItem item
            && this.ViewModel.FocusItem(item, RequestOrigin.KeyboardInput);
    }
}
