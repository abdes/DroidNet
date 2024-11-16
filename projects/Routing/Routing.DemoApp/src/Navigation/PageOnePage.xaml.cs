// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

using DroidNet.Mvvm.Generators;

/// <summary>A simple page for demonstration.</summary>
[ViewModel(typeof(PageOneViewModel))]
public sealed partial class PageOneView
{
    public PageOneView() => this.InitializeComponent();
}
