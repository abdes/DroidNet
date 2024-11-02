// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

using DroidNet.Mvvm.Generators;

/// <summary>A simple page for demonstration.</summary>
[ViewModel(typeof(PageThreeViewModel))]
public sealed partial class PageThreeView
{
    public PageThreeView() => this.InitializeComponent();
}
