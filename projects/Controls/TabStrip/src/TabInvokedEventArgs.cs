// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="TabStrip.TabInvoked"/> event.
/// </summary>
public sealed class TabInvokedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the tab item that was invoked.
    /// </summary>
    public required TabItem Item { get; init; }

    /// <summary>
    ///     Gets the index of the tab in the <see cref="TabStrip.Items"/> collection.
    ///     This index may not correspond to the visual position of the tab in the strip
    ///     if pinned tabs are present.
    /// </summary>
    public required int Index { get; init; }

    /// <summary>
    ///     Gets the parameter passed to the tab’s command, if any.
    /// </summary>
    public object? Parameter { get; init; }
}
