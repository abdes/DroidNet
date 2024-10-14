// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Helpers;
using Windows.System;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(ShellViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class ShellView
{
    public ShellView()
    {
        this.InitializeComponent();

        // TODO: Set the title bar icon by updating /Assets/WindowIcon.ico.
        // A custom title bar is required for full window theme and Mica support.
        // https://docs.microsoft.com/windows/apps/develop/title-bar?tabs=winui3#full-customization
        /*
        App.MainWindow.ExtendsContentIntoTitleBar = true;
        App.MainWindow.SetTitleBar(this.AppTitleBar);
        App.MainWindow.Activated += this.MainWindow_Activated;
        */
        this.AppTitleBarText.Text = "AppDisplayName".GetLocalized();
    }

    private static KeyboardAccelerator BuildKeyboardAccelerator(VirtualKey key, VirtualKeyModifiers? modifiers = null)
    {
        var keyboardAccelerator = new KeyboardAccelerator() { Key = key };

        if (modifiers.HasValue)
        {
            keyboardAccelerator.Modifiers = modifiers.Value;
        }

        return keyboardAccelerator;
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        // TODO: refactor custom window title bar
        // TitleBarHelper.UpdateTitleBar(this.RequestedTheme);
        this.KeyboardAccelerators.Add(BuildKeyboardAccelerator(VirtualKey.Left, VirtualKeyModifiers.Menu));
        this.KeyboardAccelerators.Add(BuildKeyboardAccelerator(VirtualKey.GoBack));

        this.ShellMenuBarSettingsButton.AddHandler(
            PointerPressedEvent,
            new PointerEventHandler(this.ShellMenuBarSettingsButton_PointerPressed),
            handledEventsToo: true);
        this.ShellMenuBarSettingsButton.AddHandler(
            PointerReleasedEvent,
            new PointerEventHandler(this.ShellMenuBarSettingsButton_PointerReleased),
            handledEventsToo: true);
    }

    private void MainWindow_Activated(object sender, WindowActivatedEventArgs args)
    {
        _ = sender; // unused

        var resource = args.WindowActivationState == WindowActivationState.Deactivated
            ? "WindowCaptionForegroundDisabled"
            : "WindowCaptionForeground";

        this.AppTitleBarText.Foreground = (SolidColorBrush)Application.Current.Resources[resource];
        App.AppTitlebar = this.AppTitleBarText;
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ShellMenuBarSettingsButton.RemoveHandler(
            PointerPressedEvent,
            (PointerEventHandler)this.ShellMenuBarSettingsButton_PointerPressed);
        this.ShellMenuBarSettingsButton.RemoveHandler(
            PointerReleasedEvent,
            (PointerEventHandler)this.ShellMenuBarSettingsButton_PointerReleased);
    }

    private void ShellMenuBarSettingsButton_PointerEntered(object sender, PointerRoutedEventArgs args)
    {
        _ = args; // unused

        AnimatedIcon.SetState((UIElement)sender, "PointerOver");
    }

    private void ShellMenuBarSettingsButton_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        _ = args; // unused

        AnimatedIcon.SetState((UIElement)sender, "Pressed");
    }

    private void ShellMenuBarSettingsButton_PointerReleased(object sender, PointerRoutedEventArgs args)
    {
        _ = args; // unused

        AnimatedIcon.SetState((UIElement)sender, "Normal");
    }

    private void ShellMenuBarSettingsButton_PointerExited(object sender, PointerRoutedEventArgs args)
    {
        _ = args; // unused

        AnimatedIcon.SetState((UIElement)sender, "Normal");
    }
}
