// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// View for MenuBar demonstration.
/// </summary>
[ViewModel(typeof(MenuBarDemoViewModel))]
public sealed partial class MenuBarDemoView : Page
{
    private bool isAltKeyPressed;
    private bool mnemonicKeyInvoked;
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
        _ = sender;
        _ = e;

        this.InitializeMenuContext();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        this.controller = null;
        this.menuContext = default;
        this.hasMenuContext = false;
        this.firstRootItem = null;
        this.ResetMnemonicTracking();
    }

    private void OnPreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (MnemonicUtilities.IsMenuKey(e.Key))
        {
            if (this.EnsureMenuContext())
            {
                var mnemonicsVisible = this.controller!.OnMnemonicModeToggled(this.menuContext);

                if (!mnemonicsVisible)
                {
                    this.ResetMnemonicTracking();
                }
                else
                {
                    this.isAltKeyPressed = true;
                    this.mnemonicKeyInvoked = false;
                }
            }

            e.Handled = true;
            return;
        }

        if (!this.isAltKeyPressed)
        {
            return;
        }

        if (this.EnsureMenuContext())
        {
            this.controller!.OnMnemonicKey(this.menuContext, (char)e.Key);
            this.mnemonicKeyInvoked = true;
            e.Handled = true;
        }
    }

    private void OnPreviewKeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (!MnemonicUtilities.IsMenuKey(e.Key))
        {
            return;
        }

        e.Handled = true;

        if (this.isAltKeyPressed && !this.mnemonicKeyInvoked)
        {
            _ = this.FocusFirstRootItem();
        }

        this.ResetMnemonicTracking();
    }

    private bool FocusFirstRootItem()
    {
        if (!this.EnsureMenuContext())
        {
            return false;
        }

        this.controller!.OnFocusRequested(
            this.menuContext,
            origin: null,
            menuItem: this.firstRootItem!,
            source: MenuInteractionActivationSource.Mnemonic,
            openSubmenu: false);

        return true;
    }

    private void ResetMnemonicTracking()
    {
        this.isAltKeyPressed = false;
        this.mnemonicKeyInvoked = false;
    }

    private bool EnsureMenuContext()
    {
        if (this.controller is not null && this.hasMenuContext && this.firstRootItem is not null)
        {
            return true;
        }

        return this.InitializeMenuContext();
    }

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

    private static class MnemonicUtilities
    {
        public static bool IsMenuKey(VirtualKey key)
            => key is VirtualKey.Menu or VirtualKey.LeftMenu or VirtualKey.RightMenu;
    }
}
