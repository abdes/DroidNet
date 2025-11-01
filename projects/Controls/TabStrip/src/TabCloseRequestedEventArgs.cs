// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="TabStrip.TabCloseRequested"/> event.
/// </summary>
/// <remarks>
///     Raised when the user requests that a tab be closed (for example, via a close button in the
///     tab header). The <see cref="TabStrip"/> only notifies the application via this event; it
///     does not remove the item from the underlying collection automatically. Applications should
///     remove the item from their model if they wish the tab to disappear.
/// <para>
///     This event is raised on the UI thread. Handlers should be fast and may be used to show a
///     confirmation UI or to cancel the close by ignoring the request (i.e. not removing the item).
/// </para>
/// </remarks>
public sealed class TabCloseRequestedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the <see cref="TabItem"/> that the user requested to close.
    /// </summary>
    /// <value>
    ///     The tab item that was targeted for closure. This property is required and is non-null
    ///     when the event is raised.
    /// </value>
    public required TabItem Item { get; init; }
}
