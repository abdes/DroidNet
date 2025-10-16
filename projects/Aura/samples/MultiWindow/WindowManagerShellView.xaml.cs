// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// View for the window manager shell demonstration.
/// </summary>
[ViewModel(typeof(WindowManagerShellViewModel))]
public sealed partial class WindowManagerShellView : Page
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WindowManagerShellView"/> class.
    /// </summary>
    public WindowManagerShellView()
    {
        this.InitializeComponent();
    }
}
