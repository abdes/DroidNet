// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DroidNet.Samples.Settings;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Shell;

/// <summary>
/// The ViewModel for the application main window shell.
/// </summary>
public partial class ShellViewModel : AbstractOutletContainer
{
    private readonly DispatcherQueue dispatcherQueue;
    private readonly IDisposable routerEventsSubscription;
    private readonly AppearanceSettingsService appearanceSettings;

    private bool isDisposed;

    [ObservableProperty]
    private bool isLightModeActive;

    private Dictionary<string, MenuItem> menuItemsLookup;

    public ShellViewModel(IRouter router, HostingContext hostingContext, AppearanceSettingsService appearanceSettings)
    {
        Debug.Assert(
            hostingContext.Dispatcher is not null,
            "DispatcherQueue in hosting context is not null when UI thread has been started");
        this.dispatcherQueue = hostingContext.Dispatcher;

        this.appearanceSettings = appearanceSettings;
        appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;

        this.InitializeSettingsMenuFlyout(); // Do this after we set this.appearanceSettings

        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));

        this.routerEventsSubscription = router.Events.OfType<ActivationComplete>()
            .Subscribe(
                @event =>
                {
                    this.Window = (Window)@event.Context.NavigationTarget;
                    this.SetupWindowTitleBar();
                });
    }

    public IEnumerable<MenuItem> MenuItems { get; private set; }

    public Window? Window { get; private set; }

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
            this.routerEventsSubscription.Dispose();
            this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_PropertyChanged;
        }

        this.isDisposed = true;
        base.Dispose(disposing);
    }

    private static void UpdateThemesSelectedItem(MenuItem themesSubMenu, MenuItem activeItem)
    {
#if DEBUG
        if (!themesSubMenu.SubItems.Contains(activeItem)) // Sanity check
        {
            throw new ArgumentException(
                $"menu item `{activeItem.Id}` is not a sub-item of `{themesSubMenu.Id}`",
                nameof(activeItem));
        }
#endif

        foreach (var item in themesSubMenu.SubItems)
        {
            item.IsSelected = false;
        }

        activeItem.IsSelected = true;
    }

    [MemberNotNull(nameof(MenuItems))]
    [MemberNotNull(nameof(menuItemsLookup))]
    private void InitializeSettingsMenuFlyout()
    {
        this.MenuItems =
        [
            new MenuItem
            {
                Text = "Settings",
                Command = this.SettingsSelectedCommand,
            },
            new MenuItem
            {
                Text = "Themes",
                Command = this.ThemeSelectedCommand,
                SubItems =
                [
                    new MenuItem
                    {
                        Text = "Dark",
                        IsSelected = this.appearanceSettings.AppThemeMode == ElementTheme.Dark,
                    },
                    new MenuItem
                    {
                        Text = "Light",
                        IsSelected = this.appearanceSettings.AppThemeMode == ElementTheme.Light,
                    },
                    new MenuItem
                    {
                        Text = "System Default",
                        IsSelected = this.appearanceSettings.AppThemeMode == ElementTheme.Default,
                    },
                ],
            },
        ];

        // Make a dictionary for fast access to menu items by a key formed by joining the ids of all
        // menu nodes in the tree to reach the target menu item.
        this.menuItemsLookup = this.MenuItems
            .SelectMany(menuItem => GetAllMenuItems(menuItem, menuItem.Id))
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase);

        static IEnumerable<KeyValuePair<string, MenuItem>> GetAllMenuItems(MenuItem menuItem, string parentId)
        {
            yield return new KeyValuePair<string, MenuItem>(parentId, menuItem);
            foreach (var subItem in menuItem.SubItems)
            {
                var subItemId = $"{parentId}.{subItem.Id}";
                foreach (var subPair in GetAllMenuItems(subItem, subItemId))
                {
                    yield return subPair;
                }
            }
        }
    }

    private void SetupWindowTitleBar()
    {
        Debug.Assert(this.Window is not null, "an activated ViewModel must always have a Window");

        this.Window.ExtendsContentIntoTitleBar = true;
        this.Window.AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
    }

    private void AppearanceSettings_PropertyChanged(object? sender, PropertyChangedEventArgs e)
        => this.dispatcherQueue.TryEnqueue(
            () => this.IsLightModeActive = this.appearanceSettings.AppThemeMode == ElementTheme.Light);

    [RelayCommand]
    private void OnThemeSelected(string menuItemFullId)
    {
        if (!this.menuItemsLookup.TryGetValue(menuItemFullId, out var menuItem))
        {
            throw new ArgumentException(
                $"cannot find the menu item for {nameof(menuItemFullId)}",
                nameof(menuItemFullId));
        }

        var lastIndex = menuItemFullId.LastIndexOf('.');
        if (lastIndex == -1 || !this.menuItemsLookup.TryGetValue(menuItemFullId[..lastIndex], out var themesSubMenu))
        {
            throw new ArgumentException(
                $"cannot find the themes submenu for {nameof(menuItemFullId)}",
                nameof(menuItemFullId));
        }

        UpdateThemesSelectedItem(themesSubMenu, menuItem);
        this.ApplyTheme(menuItem.Text);
    }

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

    [RelayCommand]
    private static void OnSettingsSelected()
    {
        // TODO: settings view and dialog
    }

    public partial class MenuItem : ObservableObject
    {
        [ObservableProperty]
        private bool isSelected;

        public required string Text { get; init; }

        /// <summary>
        /// Gets a unique identifier for the menu item by replacing any dot (`,`) in the menu item
        /// Text with an `_` and returning the resulting string as lower case.
        /// </summary>
        public string Id => this.Text.Replace('.', '_').ToLowerInvariant();

        public ICommand? Command { get; init; }

        public IEnumerable<MenuItem> SubItems { get; init; } = [];
    }
}
