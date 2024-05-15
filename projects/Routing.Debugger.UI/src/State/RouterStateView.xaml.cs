// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.State;

using DroidNet.Mvvm.Generators;

/// <summary>The view for the <see cref="RouterStateViewModel" />.</summary>
[ViewModel(typeof(RouterStateViewModel))]
public sealed partial class RouterStateView
{
    public RouterStateView() => this.InitializeComponent();
}
