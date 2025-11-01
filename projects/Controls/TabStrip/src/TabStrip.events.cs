// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

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
}
