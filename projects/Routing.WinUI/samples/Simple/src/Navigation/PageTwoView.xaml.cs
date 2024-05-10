// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>A simple page for demonstration.</summary>
[ViewModel(typeof(PageTwoViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class PageTwoView
{
    public PageTwoView() => this.InitializeComponent();
}
