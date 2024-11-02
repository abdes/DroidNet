// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Controls;

using DroidNet.Mvvm.Generators;

/// <summary>An information card used as a placeholder for a docked dockable.</summary>
[ViewModel(typeof(DockableInfoViewModel))]
public sealed partial class DockableInfoView
{
    public DockableInfoView() => this.InitializeComponent();
}
