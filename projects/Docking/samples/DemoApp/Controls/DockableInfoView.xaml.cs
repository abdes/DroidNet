// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Docking.Demo.Controls;

/// <summary>An information card used as a placeholder for a docked dockable.</summary>
[ViewModel(typeof(DockableInfoViewModel))]
public sealed partial class DockableInfoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DockableInfoView"/> class.
    /// </summary>
    public DockableInfoView()
    {
        this.InitializeComponent();
    }
}
