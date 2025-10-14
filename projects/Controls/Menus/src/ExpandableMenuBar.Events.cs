// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Specialized;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <content>
///     Event handling for <see cref="ExpandableMenuBar"/>.
/// </content>
public sealed partial class ExpandableMenuBar
{
    /// <summary>
    ///     Occurs after the control has expanded into its full menu bar state.
    /// </summary>
    public event EventHandler? Expanded;

    /// <summary>
    ///     Occurs after the control has collapsed back to its compact state.
    /// </summary>
    public event EventHandler<MenuDismissedEventArgs>? Collapsed;

    private void RaiseExpanded()
        => this.Expanded?.Invoke(this, EventArgs.Empty);

    private void RaiseCollapsed(MenuDismissKind kind)
        => this.Collapsed?.Invoke(this, new MenuDismissedEventArgs(kind));
}
