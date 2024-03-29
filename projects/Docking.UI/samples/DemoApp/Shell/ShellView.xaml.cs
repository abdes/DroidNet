// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Shell;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(ShellViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class ShellView
{
    public ShellView() => this.InitializeComponent();
}
