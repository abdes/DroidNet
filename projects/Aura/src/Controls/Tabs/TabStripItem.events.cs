// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
public partial class TabStripItem
{
    /// <summary>
    ///     Occurs when the user requests to close a tab, via a UI interaction such as clicking the
    ///     close button.
    /// </summary>
    public event EventHandler<TabCloseRequestedEventArgs>? CloseRequested;
}
