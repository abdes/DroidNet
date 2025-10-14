// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Provides data for the <see cref="MenuBar.Dismissed"/> event.
/// </summary>
public sealed class MenuDismissedEventArgs : EventArgs
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuDismissedEventArgs"/> class.
    /// </summary>
    /// <param name="kind">The dismissal kind reported by the interaction controller.</param>
    internal MenuDismissedEventArgs(MenuDismissKind kind)
    {
        this.Kind = kind;
    }

    /// <summary>
    ///     Gets the dismissal kind that triggered the event.
    /// </summary>
    public MenuDismissKind Kind { get; }
}
