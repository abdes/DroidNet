// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Debugger.UI.Config;

/// <summary>
/// A custom view for the router's configuration.
/// </summary>
[ViewModel(typeof(RoutesViewModel))]
public partial class RoutesView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesView"/> class.
    /// </summary>
    public RoutesView()
    {
        this.InitializeComponent();
    }
}
