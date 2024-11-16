// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Config;

using DroidNet.Mvvm.Generators;

/// <summary>
/// A custom view for the router's configuration.
/// </summary>
[ViewModel(typeof(RoutesViewModel))]
public partial class RoutesView
{
    public RoutesView() => this.InitializeComponent();
}
