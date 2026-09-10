// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector;

/// <summary>The current error for one field, separate from operation history.</summary>
public sealed partial class InspectorFieldDiagnostic : ObservableObject
{
    [ObservableProperty]
    public partial string Code { get; set; } = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Visibility))]
    public partial string Message { get; set; } = string.Empty;

    /// <summary>Gets whether inline error text should be visible.</summary>
    public Visibility Visibility => this.Message.Length == 0 ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the originating document revision.</summary>
    public long Revision { get; internal set; }
}
