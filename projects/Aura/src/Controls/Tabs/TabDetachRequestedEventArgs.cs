// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Event arguments raised when a tab is detached from its host. Detach is the
///     start of a tear-out operation and should not be treated as a close.
/// </summary>
public sealed class TabDetachRequestedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the logical <see cref="TabItem"/> being detached.
    /// </summary>
    public TabItem Item { get; init; } = null!;
}
