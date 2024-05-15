// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using DroidNet.Mvvm.Generators;

/// <summary>The view for the <see cref="UrlTreeViewModel" />.</summary>
[ViewModel(typeof(UrlTreeViewModel))]
public sealed partial class UrlTreeView
{
    public UrlTreeView() => this.InitializeComponent();
}
