// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Shell;

using DroidNet.Mvvm.Generators;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(ShellViewModel))]
public sealed partial class ShellView
{
    public ShellView() => this.InitializeComponent();
}
