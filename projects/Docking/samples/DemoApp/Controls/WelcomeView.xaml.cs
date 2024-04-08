// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Controls;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>A simple welcome page used as a placeholder for the main application content inside the docking workspace.</summary>
[ViewModel(typeof(WelcomeViewModel))]
[InjectAs(ServiceLifetime.Transient)]
public sealed partial class WelcomeView
{
    public WelcomeView() => this.InitializeComponent();
}
