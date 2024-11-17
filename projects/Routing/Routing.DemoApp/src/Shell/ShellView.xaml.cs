// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Demo.Shell;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(ShellViewModel))]
public sealed partial class ShellView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ShellView"/> class.
    /// </summary>
    public ShellView()
    {
        this.InitializeComponent();
    }
}
