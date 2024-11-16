// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

using DroidNet.Mvvm.Generators;

/// <summary>A simple page for demonstration.</summary>
[ViewModel(typeof(PageTwoViewModel))]
public sealed partial class PageTwoView
{
    public PageTwoView() => this.InitializeComponent();
}
