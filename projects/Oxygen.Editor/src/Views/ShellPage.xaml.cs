// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Views;

using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Helpers;
using Oxygen.Editor.Services;
using Oxygen.Editor.ViewModels;
using Windows.System;

public sealed partial class ShellPage : Page
{
    /// <summary>Initializes a new instance of the <see cref="ShellPage" /> class.</summary>
    /// <param name="viewModel"></param>
    public ShellPage(ShellViewModel viewModel)
    {
        this.ViewModel = viewModel;
        this.InitializeComponent();

        this.ViewModel.NavigationService.Frame = this.NavigationFrame;

        // TODO: Set the title bar icon by updating /Assets/WindowIcon.ico.
        // A custom title bar is required for full window theme and Mica support.
        // https://docs.microsoft.com/windows/apps/develop/title-bar?tabs=winui3#full-customization
        App.MainWindow.ExtendsContentIntoTitleBar = true;
        App.MainWindow.SetTitleBar(this.AppTitleBar);
        App.MainWindow.Activated += this.MainWindow_Activated;
        this.AppTitleBarText.Text = "AppDisplayName".GetLocalized();
    }

    public ShellViewModel ViewModel
    {
        get;
    }

    private static KeyboardAccelerator BuildKeyboardAccelerator(VirtualKey key, VirtualKeyModifiers? modifiers = null)
    {
        var keyboardAccelerator = new KeyboardAccelerator() { Key = key };

        if (modifiers.HasValue)
        {
            keyboardAccelerator.Modifiers = modifiers.Value;
        }

        keyboardAccelerator.Invoked += OnKeyboardAcceleratorInvoked;

        return keyboardAccelerator;
    }

    private static void OnKeyboardAcceleratorInvoked(
        KeyboardAccelerator sender,
        KeyboardAcceleratorInvokedEventArgs args)
    {
        var navigationService = Ioc.Default.GetRequiredService<INavigationService>();

        var result = navigationService.GoBack();

        args.Handled = result;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        TitleBarHelper.UpdateTitleBar(this.RequestedTheme);

        this.KeyboardAccelerators.Add(BuildKeyboardAccelerator(VirtualKey.Left, VirtualKeyModifiers.Menu));
        this.KeyboardAccelerators.Add(BuildKeyboardAccelerator(VirtualKey.GoBack));

        this.ShellMenuBarSettingsButton.AddHandler(
            PointerPressedEvent,
            new PointerEventHandler(this.ShellMenuBarSettingsButton_PointerPressed),
            true);
        this.ShellMenuBarSettingsButton.AddHandler(
            PointerReleasedEvent,
            new PointerEventHandler(this.ShellMenuBarSettingsButton_PointerReleased),
            true);
    }

    private void MainWindow_Activated(object sender, WindowActivatedEventArgs args)
    {
        var resource = args.WindowActivationState == WindowActivationState.Deactivated
            ? "WindowCaptionForegroundDisabled"
            : "WindowCaptionForeground";

        this.AppTitleBarText.Foreground = (SolidColorBrush)Application.Current.Resources[resource];
        App.AppTitlebar = this.AppTitleBarText;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        this.ShellMenuBarSettingsButton.RemoveHandler(
            PointerPressedEvent,
            (PointerEventHandler)this.ShellMenuBarSettingsButton_PointerPressed);
        this.ShellMenuBarSettingsButton.RemoveHandler(
            PointerReleasedEvent,
            (PointerEventHandler)this.ShellMenuBarSettingsButton_PointerReleased);
    }

    private void ShellMenuBarSettingsButton_PointerEntered(object sender, PointerRoutedEventArgs e)
        => AnimatedIcon.SetState((UIElement)sender, "PointerOver");

    private void ShellMenuBarSettingsButton_PointerPressed(object sender, PointerRoutedEventArgs e)
        => AnimatedIcon.SetState((UIElement)sender, "Pressed");

    private void ShellMenuBarSettingsButton_PointerReleased(object sender, PointerRoutedEventArgs e)
        => AnimatedIcon.SetState((UIElement)sender, "Normal");

    private void ShellMenuBarSettingsButton_PointerExited(object sender, PointerRoutedEventArgs e)
        => AnimatedIcon.SetState((UIElement)sender, "Normal");
}
