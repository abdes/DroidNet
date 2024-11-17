// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A simple page for demonstration.
/// </summary>
[ViewModel(typeof(PageOneViewModel))]
public sealed partial class PageOneView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PageOneView"/> class.
    /// </summary>
    public PageOneView()
    {
        this.InitializeComponent();
    }
}
