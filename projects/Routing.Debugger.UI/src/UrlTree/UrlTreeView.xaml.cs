// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml.Controls;

/// <summary>The view for the <see cref="UrlTreeViewModel" />.</summary>
[ViewModel(typeof(UrlTreeViewModel))]
public sealed partial class UrlTreeView : UserControl
{
    public UrlTreeView() => this.InitializeComponent();
}
