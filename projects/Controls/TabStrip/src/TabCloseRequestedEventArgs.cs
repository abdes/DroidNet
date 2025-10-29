// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="TabStrip.TabCloseRequested"/> event.
/// </summary>
public sealed class TabCloseRequestedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the tab item that the user requested to close.
    /// </summary>
    public required TabItem Item { get; init; }
}
