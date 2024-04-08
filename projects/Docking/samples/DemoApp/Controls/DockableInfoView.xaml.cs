// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Controls;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>An information card used as a placeholder for a docked dockable.</summary>
[ViewModel(typeof(DockableInfoViewModel))]
[InjectAs(ServiceLifetime.Transient)]
public sealed partial class DockableInfoView
{
    public DockableInfoView() => this.InitializeComponent();
}
