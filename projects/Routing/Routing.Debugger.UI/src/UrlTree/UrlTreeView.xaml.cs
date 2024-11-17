// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Debugger.UI.UrlTree;

/// <summary>The view for the <see cref="UrlTreeViewModel" />.</summary>
[ViewModel(typeof(UrlTreeViewModel))]
public sealed partial class UrlTreeView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="UrlTreeView"/> class.
    /// </summary>
    public UrlTreeView()
    {
        this.InitializeComponent();
    }
}
