// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura;
using DroidNet.Aura.Decoration;
using Microsoft.UI;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// Display model for window information.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ObservableObject")]
public sealed partial class WindowInfo : ObservableObject
{
    /// <summary>
    /// Gets or sets the window unique identifier.
    /// </summary>
    [ObservableProperty]
    public partial WindowId Id { get; set; }

    /// <summary>
    /// Gets or sets the window title.
    /// </summary>
    [ObservableProperty]
    public partial string Title { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the window type.
    /// </summary>
    [ObservableProperty]
    public partial WindowCategory? Category { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether the window is active.
    /// </summary>
    [ObservableProperty]
    public partial bool IsActive { get; set; }

    /// <summary>
    /// Gets or sets the creation timestamp.
    /// </summary>
    [ObservableProperty]
    public partial DateTimeOffset CreatedAt { get; set; }

    /// <summary>
    /// Gets or sets the backdrop kind for this window.
    /// </summary>
    [ObservableProperty]
    public partial BackdropKind Backdrop { get; set; }
}
