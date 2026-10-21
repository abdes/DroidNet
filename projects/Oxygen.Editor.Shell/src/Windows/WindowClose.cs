// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>The close of one window, as the participants of closing see it.</summary>
public sealed record WindowClose
{
    /// <summary>Gets the window that closes.</summary>
    public required WindowId Window { get; init; }

    /// <summary>
    ///     Gets a value indicating whether no other window of the editor is open. Unless a participant opens another
    ///     window, the close ends the editor.
    /// </summary>
    public required bool IsLastWindow { get; init; }
}
