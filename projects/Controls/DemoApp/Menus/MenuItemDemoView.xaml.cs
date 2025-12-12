// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Menus;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuItem control with live property editing capabilities.
/// </summary>
[ViewModel(typeof(MenuItemDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
public sealed partial class MenuItemDemoView : Page
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuItemDemoView"/> class.
    /// </summary>
    public MenuItemDemoView()
    {
        this.InitializeComponent();
    }

    // Delegate to ViewModel to handle the invoked event
    private void DemoMenuItem_OnInvoked(object sender, MenuItemInvokedEventArgs e)
        => this.ViewModel!.OnMenuItemInvoked(e);
}
