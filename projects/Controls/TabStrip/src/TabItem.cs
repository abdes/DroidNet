// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Represents a single entry in a <see cref="TabStrip"/>, defining the tab’s header, icon,
///     command, and state. A <see cref="TabItem"/> is a lightweight data model consumed by the
///     control to render and interact with tabs, rather than a visual element itself.
/// </summary>
public sealed partial class TabItem : ObservableObject
{
    /// <summary>
    ///     Gets or sets the text displayed as the tab’s label.
    /// </summary>
    [ObservableProperty]
    public partial string Header { get; set; }

    /// <summary>
    ///     Gets or sets the optional icon displayed alongside the header.
    /// </summary>
    [ObservableProperty]
    public partial IconSource? Icon { get; set; }

    /// <summary>
    ///     Gets or sets the command that is executed when the tab is invoked.
    /// </summary>
    [ObservableProperty]
    public partial ICommand Command { get; set; }

    /// <summary>
    ///     Gets or sets an optional parameter passed to the <see cref="Command"/>.
    /// </summary>
    [ObservableProperty]
    public partial object? CommandParameter { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the tab displays a close button.
    /// </summary>
    [ObservableProperty]
    public partial bool IsClosable { get; set; } = true;

    /// <summary>
    ///     Gets or sets a value indicating whether the tab is pinned (always visible and not draggable).
    /// </summary>
    [ObservableProperty]
    public partial bool IsPinned { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the tab is currently selected.
    /// </summary>
    [ObservableProperty]
    public partial bool IsSelected { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether this is a temporary placeholder item
    ///     used during drag-and-drop reordering. Placeholder items have a lightweight template
    ///     and are never selected or activated.
    /// </summary>
    [ObservableProperty]
    public partial bool IsPlaceholder { get; set; }
}
