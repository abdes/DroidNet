// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.UI;

namespace DroidNet.Aura;

/// <summary>
/// Provides an implementation of the <see cref="IAppThemeModeService"/> interface to manage and apply theme modes to application windows.
/// </summary>
/// <remarks>
/// This service is responsible for applying the specified theme mode to the content of a window and its title bar. It listens for changes in appearance settings and updates the theme accordingly.
/// </remarks>
public sealed partial class AppThemeModeService : IAppThemeModeService, IDisposable
{
    private readonly AppearanceSettingsService appearanceSettings;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="AppThemeModeService"/> class.
    /// </summary>
    /// <param name="appearanceSettings">The appearance settings service used to manage theme settings.</param>
    public AppThemeModeService(AppearanceSettingsService appearanceSettings)
    {
        this.appearanceSettings = appearanceSettings;
        this.appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "no fatal exceptions and nothing more to do than just logging")]
    public void ApplyThemeMode(Window window, ElementTheme requestedThemeMode)
    {
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
            contentElement.RequestedTheme = requestedThemeMode;

            // Apply theme to TitleBar buttons
            var titleBar = window.AppWindow.TitleBar;
            if (titleBar is null)
            {
                return;
            }

            if (requestedThemeMode == ElementTheme.Light)
            {
                ApplyLightThemeToTitleBar(titleBar);
            }
            else if (requestedThemeMode == ElementTheme.Dark)
            {
                ApplyDarkThemeToTitleBar(titleBar);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to change theme mode of the app. {ex}");
        }
    }

    /// <inheritdoc/>
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

    /// <summary>
    /// Applies the dark theme to the title bar of the specified window.
    /// </summary>
    /// <param name="titleBar">The title bar to which the dark theme will be applied.</param>
    private static void ApplyDarkThemeToTitleBar(AppWindowTitleBar titleBar)
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

    /// <summary>
    /// Applies the light theme to the title bar of the specified window.
    /// </summary>
    /// <param name="titleBar">The title bar to which the light theme will be applied.</param>
    private static void ApplyLightThemeToTitleBar(AppWindowTitleBar titleBar)
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
        titleBar.ButtonPressedBackgroundColor = Color.FromArgb(40, 0, 0, 0);
    }

    /// <summary>
    /// Handles changes to the <see cref="IAppearanceSettings.AppThemeMode"/> property to update the theme mode when the setting changes.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void AppearanceSettings_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal) == true)
        {
            Debug.WriteLine("AppThemeModeService: theme mode setting changed");
        }
    }
}
