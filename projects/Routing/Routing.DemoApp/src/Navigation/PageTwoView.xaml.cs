// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A simple page for demonstration.
/// </summary>
[ViewModel(typeof(PageTwoViewModel))]
public sealed partial class PageTwoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PageTwoView"/> class.
    /// </summary>
    public PageTwoView()
    {
        this.InitializeComponent();
    }
}
