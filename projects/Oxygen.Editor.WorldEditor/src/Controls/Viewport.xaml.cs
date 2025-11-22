// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// A control that displays a 3D viewport with overlay controls.
/// </summary>
public sealed partial class Viewport : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="Viewport"/> class.
    /// </summary>
    public Viewport()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Gets or sets the ViewModel for this viewport.
    /// </summary>
    public ViewportViewModel? ViewModel
    {
        get => this.DataContext as ViewportViewModel;
        set => this.DataContext = value;
    }
}
