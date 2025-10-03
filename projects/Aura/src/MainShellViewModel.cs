// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura;

/// <summary>
/// Represents the view model for the main shell of the application, providing decorations and enhancements to the window content.
/// </summary>
/// <remarks>
/// The <see cref="MainShellViewModel"/> class is responsible for managing the main shell view of the application. It handles window-related events, manages appearance settings, and provides a customizable menu for user interactions.
/// It decorates the window with a custom title bar, an application icon, and provides a collapsible main menu and a flyout menu for settings and theme selection.
/// </remarks>
public partial class MainShellViewModel : AbstractOutletContainer
{
    private readonly DispatcherQueue dispatcherQueue;
    private readonly AppearanceSettingsService appearanceSettings;
    private MenuItemData? themesMenuItem;

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainShellViewModel"/> class.
    /// </summary>
    /// <param name="router">The router used for navigation.</param>
    /// <param name="hostingContext">The hosting context containing dispatcher and application information.</param>
    /// <param name="appearanceSettings">The appearance settings service used to manage theme settings.</param>
    public MainShellViewModel(IRouter router, HostingContext hostingContext, AppearanceSettingsService appearanceSettings)
    {
        Debug.Assert(
            hostingContext.Dispatcher is not null,
            "DispatcherQueue in hosting context is not null when UI thread has been started");
        this.dispatcherQueue = hostingContext.Dispatcher;

        this.appearanceSettings = appearanceSettings;
        appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;

        this.InitializeSettingsMenu(); // Do this after we set this.appearanceSettings
        this.SettingsMenu = this.MenuBuilder.Build();
        this.SynchronizeThemeSelection();

        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));

        _ = router.Events.OfType<ActivationComplete>()
            .Take(1) // Do this only on the first activation and then unsubscribe
            .Subscribe(
                @event =>
                {
                    this.Window = (Window)@event.Context.NavigationTarget;
                    this.SetupWindowTitleBar();
                });
    }

    /// <summary>
    /// Gets the menu builder for creating the settings menu.
    /// </summary>
    public MenuBuilder MenuBuilder { get; } = new MenuBuilder();

    /// <summary>
    /// Gets the menu source consumed by menu controls for settings and themes.
    /// </summary>
    public IMenuSource SettingsMenu { get; }

    [ObservableProperty]
    public partial bool IsLightModeActive { get; set; }

    /// <summary>
    /// Gets the window associated with this view model.
    /// </summary>
    public Window? Window { get; private set; }

    /// <summary>
    /// Gets the content view model for the primary outlet.
    /// </summary>
    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;

    /// <inheritdoc/>
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_PropertyChanged;
        }

        this.isDisposed = true;
        base.Dispose(disposing);
    }

    /// <summary>
    /// Updates the selected item in the themes submenu.
    /// </summary>
    /// <param name="themesSubMenu">The themes submenu.</param>
    /// <param name="activeItem">The active menu item to be selected.</param>
    private static void UpdateThemesSelectedItem(MenuItemData themesSubMenu, MenuItemData activeItem)
    {
#if DEBUG
        // Sanity check
        if (!themesSubMenu.SubItems.Contains(activeItem))
        {
            throw new ArgumentException(
                $"menu item `{activeItem.Id}` is not a sub-item of `{themesSubMenu.Id}`",
                nameof(activeItem));
        }
#endif

        foreach (var item in themesSubMenu.SubItems)
        {
            item.IsChecked = false;
        }

        activeItem.IsChecked = true;
    }

    /// <summary>
    /// Command handler for settings selection.
    /// </summary>
    [RelayCommand]
    private static void OnSettingsSelected()
    {
        // TODO: settings view and dialog
    }

    /// <summary>
    /// Initializes the settings menu flyout with theme options.
    /// </summary>
    private void InitializeSettingsMenu()
    {
        var themesSubMenu = new MenuItemData
        {
            Text = "Themes",
            SubItems =
            [
                new MenuItemData
                {
                    Text = "Dark",
                    RadioGroupId = "THEME_MODE",
                    IsChecked = this.appearanceSettings.AppThemeMode == ElementTheme.Dark,
                    Command = this.ThemeSelectedCommand,
                },
                new MenuItemData
                {
                    Text = "Light",
                    RadioGroupId = "THEME_MODE",
                    IsChecked = this.appearanceSettings.AppThemeMode == ElementTheme.Light,
                    Command = this.ThemeSelectedCommand,
                },
                new MenuItemData
                {
                    Text = "System Default",
                    RadioGroupId = "THEME_MODE",
                    IsChecked = this.appearanceSettings.AppThemeMode == ElementTheme.Default,
                    Command = this.ThemeSelectedCommand,
                },
            ],
        };

        this.MenuBuilder
            .AddMenuItem(new MenuItemData
            {
                Text = "Settings",
                Command = this.SettingsSelectedCommand,
            })
            .AddMenuItem(themesSubMenu);

        this.themesMenuItem = themesSubMenu;
    }

    private void SynchronizeThemeSelection()
    {
        if (this.themesMenuItem is null)
        {
            return;
        }

        var activeItem = this.appearanceSettings.AppThemeMode switch
        {
            ElementTheme.Dark => this.themesMenuItem.SubItems.FirstOrDefault(item => string.Equals(item.Text, "Dark", StringComparison.OrdinalIgnoreCase)),
            ElementTheme.Light => this.themesMenuItem.SubItems.FirstOrDefault(item => string.Equals(item.Text, "Light", StringComparison.OrdinalIgnoreCase)),
            _ => this.themesMenuItem.SubItems.FirstOrDefault(item => string.Equals(item.Text, "System Default", StringComparison.OrdinalIgnoreCase)),
        };

        if (activeItem is not null)
        {
            UpdateThemesSelectedItem(this.themesMenuItem, activeItem);
        }
    }

    /// <summary>
    /// Sets up the window title bar with custom decorations and enhancements.
    /// </summary>
    private void SetupWindowTitleBar()
    {
        Debug.Assert(this.Window is not null, "an activated ViewModel must always have a Window");

        this.Window.ExtendsContentIntoTitleBar = true;
        this.Window.AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Standard;
    }

    /// <summary>
    /// Handles property changes in the appearance settings and updates the light mode state.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    private void AppearanceSettings_PropertyChanged(object? sender, PropertyChangedEventArgs e)
        => this.dispatcherQueue.TryEnqueue(
            () =>
            {
                this.IsLightModeActive = this.appearanceSettings.AppThemeMode == ElementTheme.Light;
                this.SynchronizeThemeSelection();
            });

    /// <summary>
    /// Command handler for theme selection.
    /// </summary>
    /// <param name="menuItem">The menu item selected by the user.</param>
    [RelayCommand]
    private void OnThemeSelected(MenuItemData menuItem)
    {
        if (menuItem is null)
        {
            throw new ArgumentException(
                $"cannot handle null {nameof(menuItem)}",
                nameof(menuItem));
        }

        this.ApplyTheme(menuItem.Text);
        this.SynchronizeThemeSelection();
    }

    /// <summary>
    /// Applies the selected theme to the application.
    /// </summary>
    /// <param name="themeName">The name of the selected theme.</param>
    private void ApplyTheme(string themeName)
    {
        if (string.Equals(
                this.appearanceSettings.AppThemeMode.ToString(),
                themeName,
                StringComparison.OrdinalIgnoreCase))
        {
            // Already the current theme
            return;
        }

        if (themeName.Contains("Dark", StringComparison.OrdinalIgnoreCase))
        {
            this.appearanceSettings.AppThemeMode = ElementTheme.Dark;
        }
        else if (themeName.Contains("Light", StringComparison.OrdinalIgnoreCase))
        {
            this.appearanceSettings.AppThemeMode = ElementTheme.Light;
        }
        else if (themeName.Contains("Default", StringComparison.OrdinalIgnoreCase))
        {
            this.appearanceSettings.AppThemeMode = ElementTheme.Default;
        }
        else
        {
            // Maintain existing theme if no match
            this.appearanceSettings.AppThemeMode = this.appearanceSettings.AppThemeMode;
        }
    }
}
