// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Services;

using System.Diagnostics;
using DroidNet.Samples.Settings;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Windows.UI;

public sealed partial class AppThemeModeService : IAppThemeModeService, IDisposable
{
    private readonly AppearanceSettingsService appearanceSettings;
    private bool isDisposed;

    public AppThemeModeService(AppearanceSettingsService appearanceSettings)
    {
        this.appearanceSettings = appearanceSettings;
        this.appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;
    }

    public void ApplyThemeMode(Window window, ElementTheme requestedThemeMode)
    {
        // TODO: Optimize, when current theme is same than new theme, do nothing

        var contentElement = window.Content as FrameworkElement ?? throw new ArgumentException(
            $"window content element is not {nameof(FrameworkElement)}",
            nameof(window));

        if (requestedThemeMode == ElementTheme.Default)
        {
            requestedThemeMode = Application.Current.RequestedTheme == ApplicationTheme.Light
                ? ElementTheme.Light
                : ElementTheme.Dark;
        }

        try
        {
            // Apply theme to window content
            contentElement.RequestedTheme = this.appearanceSettings.AppThemeMode;

            // Apply theme to TitleBar buttons
            var titleBar = window.AppWindow.TitleBar;
            if (titleBar is null)
            {
                return;
            }

            if (requestedThemeMode == ElementTheme.Light)
            {
                titleBar.ForegroundColor = Colors.Black;
                titleBar.BackgroundColor = Colors.White;
                titleBar.InactiveForegroundColor = Colors.Gray;
                titleBar.InactiveBackgroundColor = Colors.White;

                titleBar.ButtonForegroundColor = Colors.Black;
                titleBar.ButtonBackgroundColor = Colors.Transparent;
                titleBar.ButtonInactiveForegroundColor = Colors.DimGray;
                titleBar.ButtonInactiveBackgroundColor = Colors.Transparent;

                titleBar.ButtonHoverForegroundColor = Colors.Black;
                titleBar.ButtonHoverBackgroundColor = Color.FromArgb(51, 0, 0, 0);
                titleBar.ButtonPressedForegroundColor = Colors.Black;
                titleBar.ButtonPressedBackgroundColor = Colors.White;
            }
            else if (requestedThemeMode == ElementTheme.Dark)
            {
                titleBar.ForegroundColor = Colors.White;
                titleBar.BackgroundColor = Color.FromArgb(255, 31, 31, 31);
                titleBar.InactiveForegroundColor = Colors.Gray;
                titleBar.InactiveBackgroundColor = Color.FromArgb(255, 31, 31, 31);

                titleBar.ButtonForegroundColor = Colors.White;
                titleBar.ButtonBackgroundColor = Colors.Transparent;
                titleBar.ButtonInactiveForegroundColor = Colors.Gray;
                titleBar.ButtonInactiveBackgroundColor = Colors.Transparent;

                titleBar.ButtonHoverForegroundColor = Colors.White;
                titleBar.ButtonHoverBackgroundColor = Color.FromArgb(51, 255, 255, 255);
                titleBar.ButtonPressedForegroundColor = Colors.White;
                titleBar.ButtonPressedBackgroundColor = Colors.Gray;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to change theme mode of the app. {ex}");
        }
    }

    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_PropertyChanged;
        GC.SuppressFinalize(this);
    }

    private void AppearanceSettings_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal) == true)
        {
            Debug.WriteLine("AppThemeModeService: theme mode setting changed");
        }
    }
}
