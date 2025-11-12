// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

#pragma warning disable CS0067 // Event is never used

/// <summary>
///     A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///     and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
public partial class TabStrip
{
    /// <summary>
    ///     Occurs when the selected tabs in the <see cref="TabStrip"/> changes.
    /// </summary>
    /// <remarks>
    ///     This event is raised when the user selects or deselects a tab, either via direct
    ///     interaction (e.g. clicking on a tab) or programmatically (e.g. changing the <see
    ///     cref="SelectedItem"/> property).
    ///     <para>
    ///     <see cref="TabStrip"/> control supports single and multiple selection modes. In single
    ///     selection mode, only one tab can be selected at a time. In multiple selection mode,
    ///     users can select multiple tabs using modifier keys (e.g. Ctrl, Shift).
    ///     </para>
    ///     <para>
    ///     During a drag operation, any multi-selection is cleared and only the dragged tab remains
    ///     selected. When a hidden placeholder is inserted into a destination <see
    ///     cref="TabStrip"/> during a drag, the control performs a layout pass, restores
    ///     visibility, and raises <c>SelectionChanged</c> followed by <c>TabActivated</c>.
    ///     Handlers should treat these events as informational during an active drag and avoid
    ///     mutating the strip's collection or drag state.
    ///     </para>
    ///     <para><b>Note:</b> Selection does not automatically activate the tab. See <see
    ///     cref="TabActivated"/> for more information.</para>
    /// </remarks>
    public event EventHandler<TabSelectionChangedEventArgs>? SelectionChanged;

    /// <summary>
    ///     Occurs when a tab is activated via explicit user interaction or programmatically.
    /// </summary>
    /// <remarks>
    ///     With a pointer (e.g . mouse, touch), tab activation occurs when the user taps or clicks
    ///     on a tab. Note that during multiple selection scenarios, clicking a tab to add it to the
    ///     selection or to extend the selection up to that tab, will activate the tab as well. But,
    ///     clicking a tab that is already selected to exclude it from the selection will not
    ///     activate the tab.
    ///     <para>
    ///     With keyboard navigation, this event is raised when the user presses Enter or Space
    ///     while a tab has focus, or when a keyboard shortcut is used to activate the tab. Gaining
    ///     focus in itself, or navigating to the tab using the arrow keys will not activate the
    ///     tab.
    ///     </para>
    /// </remarks>
    public event EventHandler<TabActivatedEventArgs>? TabActivated;

    /// <summary>
    ///     Occurs when the user requests to close a tab, via a UI interaction such as clicking the
    ///     close button.
    /// </summary>
    /// <remarks>
    ///     This is the only event that really matters from a <see cref="TabStrip"/> control point
    ///     of view. It is raised to notify the application to remove the corresponding <see
    ///     cref="TabItem"/> from the underlying collection, if it wants to. If the application does
    ///     not remove the item, the tab will remain as-is. The <see cref="TabStrip"/> control does
    ///     nothing more than raise this event.
    /// </remarks>
    public event EventHandler<TabCloseRequestedEventArgs>? TabCloseRequested;

    /// <summary>
    ///     Occurs when the user begins tearing out a tab (tear-out start). This is a
    ///     request to detach the tab from its host window, not a request to close it.
    /// </summary>
    /// <remarks>
    ///     Raised to notify the application that a tab is being detached for tear-out.
    ///     This event should not be used to veto an operation; use the application-level
    ///     document closing contract when the user explicitly requests to close a document.
    /// </remarks>
    public event EventHandler<TabDetachRequestedEventArgs>? TabDetachRequested;

    /// <summary>
    ///     Occurs when a drag session needs a lightweight preview image for the dragged <see
    ///     cref="TabItem"/>. Handlers may synchronously set <see
    ///     cref="TabDragImageRequestEventArgs.PreviewBitmap"/> to customize the overlay.
    /// </summary>
    /// <remarks>
    ///     Raised on the UI thread when the dragged tab leaves its originating <see
    ///     cref="TabStrip"/> (tear-out begins). Handlers must be fast and avoid blocking I/O.
    ///     Prefer a ready-to-render <see cref="Windows.Graphics.Imaging.SoftwareBitmap"/> set synchronously.
    ///     If no preview is provided, the control
    ///     renders a compact fallback visual. After this event, the control raises
    ///     <c>TabCloseRequested</c> to signal tear-out has begun.
    ///     <para>
    ///     Exceptions thrown by handlers are caught; faulty previews are ignored and the drag
    ///     continues with the fallback visual. See <see cref="TabDragComplete"/> for completion
    ///     semantics.
    ///     </para>
    /// </remarks>
    public event EventHandler<TabDragImageRequestEventArgs>? TabDragImageRequest;

    /// <summary>
    ///     Occurs when a drag operation completes.
    /// </summary>
    /// <remarks>
    ///     Reports the final placement of the dragged <see cref="TabItem"/>. If the item was not
    ///     inserted into a destination <see cref="TabStrip"/> (for example, dropped outside any
    ///     strip or the drag failed), both <see cref="TabDragCompleteEventArgs.DestinationStrip"/>
    ///     and <see cref="TabDragCompleteEventArgs.NewIndex"/> will be <see langword="null"/>.
    ///     <para>
    ///     Raised after layout passes and placeholder insertion/removal. Handlers should be fast
    ///     and avoid long-running work on the UI thread.
    ///     </para>
    ///     <para>
    ///     Exceptions thrown by handlers are caught and will not prevent the control from
    ///     completing the drag and surfacing the final state.
    ///     </para>
    /// </remarks>
    public event EventHandler<TabDragCompleteEventArgs>? TabDragComplete;

    /// <summary>
    ///     Raised when the user drops a tab outside any <see cref="TabStrip"/>.
    /// </summary>
    /// <remarks>
    ///     The application is expected to create a new top-level window and host the <see
    ///     cref="TabItem"/>. The <see cref="TabTearOutRequestedEventArgs.ScreenDropPoint"/> provides
    ///     the drop point in screen coordinates and may be used as the initial position for the new
    ///     window.
    ///     <para>
    ///     Raised on the UI thread. If a handler throws, the control catches the exception and
    ///     completes the drag; handlers should avoid relying on exceptions for control flow.
    ///     </para>
    /// </remarks>
    public event EventHandler<TabTearOutRequestedEventArgs>? TabTearOutRequested;
}
