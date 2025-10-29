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
    ///     Occurs when a tab is invoked (clicked).
    /// </summary>
    public event EventHandler<TabInvokedEventArgs>? TabInvoked;

    /// <summary>
    ///     Occurs when the selected tab changes.
    /// </summary>
    public event EventHandler<TabSelectionChangedEventArgs>? SelectionChanged;

    /// <summary>
    ///     Occurs when the user requests to close a tab, via a UI interaction such as clicking the close button.
    /// </summary>
    /// <remarks>
    ///     This is the only event that really matters from a TabStrip control point of view. It is raised to notify the
    ///     application to remove the corresponding <see cref="TabItem"/> from the underlying collection, if it wants
    ///     to. If the application does not remove the item, the tab will remain as-is. The <see cref="TabStrip"/>
    ///     control does nothing more than raise this event.
    /// </remarks>
    public event EventHandler<TabCloseRequestedEventArgs>? TabCloseRequested;
}
