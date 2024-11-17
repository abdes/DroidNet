// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Debugger.UI.State;

/// <summary>The view for the <see cref="RouterStateViewModel" />.</summary>
[ViewModel(typeof(RouterStateViewModel))]
public sealed partial class RouterStateView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RouterStateView"/> class.
    /// </summary>
    public RouterStateView()
    {
        this.InitializeComponent();
    }
}
