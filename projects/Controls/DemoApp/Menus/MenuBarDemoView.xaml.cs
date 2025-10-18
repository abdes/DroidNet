// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Menus;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// View for demonstrating both the standard <see cref="DroidNet.Controls.Menus.MenuBar"/> and the <see cref="DroidNet.Controls.Menus.ExpandableMenuBar"/>.
/// </summary>
[ViewModel(typeof(MenuBarDemoViewModel))]
public sealed partial class MenuBarDemoView : Page
{
    private MenuInteractionController? controller;
    private MenuInteractionContext menuContext;
    private bool hasMenuContext;
    private MenuItemData? firstRootItem;

    /// <summary>
    /// Initializes a new instance of the <see cref="MenuBarDemoView"/> class.
    /// </summary>
    public MenuBarDemoView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        _ = this.InitializeMenuContext();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        this.controller = null;
        this.menuContext = default;
        this.hasMenuContext = false;
        this.firstRootItem = null;
    }

    private bool EnsureMenuContext()
        => (this.controller is not null && this.hasMenuContext && this.firstRootItem is not null) || this.InitializeMenuContext();

    private bool InitializeMenuContext()
    {
        if (this.DemoMenuBar is not { } menuBar || this.ViewModel?.MenuBarSource is not { } menuSource || menuSource.Items.Count == 0)
        {
            this.controller = null;
            this.menuContext = default;
            this.hasMenuContext = false;
            this.firstRootItem = null;
            return false;
        }

        this.controller = menuSource.Services.InteractionController;
        this.menuContext = MenuInteractionContext.ForRoot(menuBar);
        this.hasMenuContext = true;
        this.firstRootItem = menuSource.Items[0];
        return true;
    }
}
