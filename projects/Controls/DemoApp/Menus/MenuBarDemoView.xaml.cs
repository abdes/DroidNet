// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// View for MenuBar demonstration.
/// </summary>
[ViewModel(typeof(MenuBarDemoViewModel))]
public sealed partial class MenuBarDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuBarDemoView"/> class.
    /// </summary>
    public MenuBarDemoView()
    {
        this.InitializeComponent();
    }
}
