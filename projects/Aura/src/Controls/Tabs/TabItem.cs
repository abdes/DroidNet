// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura.Drag;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents a single entry in a <see cref="TabStrip"/>, defining the tab’s header, icon,
///     command, and state. A <see cref="TabItem"/> is a lightweight data model consumed by the
///     control to render and interact with tabs, rather than a visual element itself.
/// </summary>
public sealed partial class TabItem : ObservableObject, IDragPayload, IEquatable<TabItem>
{
    /// <summary>
    ///     Gets the stable identifier for the payload's content. Preserved across shallow clones,
    ///     and intentionally not observable as it should never change.
    /// </summary>
    /// <remarks>
    ///     ContentId must be assignable by generated XAML code (internal setter) while remaining
    ///     non-public to consumers. Using an internal setter avoids the CS8852 init-only assignment
    ///     error from XAML-generated code while preserving external immutability.
    /// </remarks>
    public Guid ContentId { get; internal set; } = Guid.NewGuid();

    /// <summary>
    ///     Gets a human-readable title used by the drag infrastructure. Intentionally not
    ///     observable as it is only needed for drag operations. Maps to <see cref="Header"/>.
    /// </summary>
    public string Title => this.Header ?? string.Empty;

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
    ///     Produces a shallow clone with identical <see cref="ContentId"/> so the coordinator
    ///     can safely operate on a distinct reference while the application removes the original.
    /// </summary>
    /// <returns>A shallow-cloned <see cref="TabItem"/> that preserves <see cref="ContentId"/>.</returns>
    public IDragPayload ShallowClone()
    {
        return new TabItem
        {
            ContentId = this.ContentId,
            Header = this.Header,
            Icon = this.Icon,
            Command = this.Command,
            CommandParameter = this.CommandParameter,
            IsClosable = this.IsClosable,
            IsPinned = this.IsPinned,
            IsSelected = this.IsSelected,
        };
    }

    /// <inheritdoc/>
    public bool Equals(IDragPayload? other) => other is not null && other.ContentId == this.ContentId;

    /// <inheritdoc/>
    public bool Equals(TabItem? other) => other is not null && other.ContentId == this.ContentId;

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is IDragPayload payload && this.Equals(payload);

    /// <inheritdoc/>
    public override int GetHashCode() => this.ContentId.GetHashCode();
}
