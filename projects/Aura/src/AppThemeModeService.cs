// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Animation;
using Windows.UI;

namespace DroidNet.Aura;

/// <summary>
///     Provides an implementation of the <see cref="IAppThemeModeService"/> interface to manage and apply theme modes to application windows.
/// </summary>
/// <remarks>
///     This service is responsible for applying the specified theme mode to the content of a window and its title bar. It listens
///     for changes in appearance settings and updates the theme accordingly.
/// </remarks>
public sealed partial class AppThemeModeService : IAppThemeModeService, IDisposable
{
    private readonly ISettingsService<IAppearanceSettings> appearanceSettings;
    private readonly ILogger<AppThemeModeService> logger;
    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="AppThemeModeService"/> class.
    /// </summary>
    /// <param name="appearanceSettings">The appearance settings service used to manage theme settings.</param>
    /// <param name="loggerFactory">The logger factory for creating loggers.</param>
    public AppThemeModeService(ISettingsService<IAppearanceSettings> appearanceSettings, ILoggerFactory? loggerFactory = null)
    {
        this.appearanceSettings = appearanceSettings;
        this.logger = loggerFactory?.CreateLogger<AppThemeModeService>() ?? NullLogger<AppThemeModeService>.Instance;
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
            // Enable smooth theme transitions if not already enabled
            EnsureThemeTransitions(contentElement);

            // Apply theme to window content
            contentElement.RequestedTheme = requestedThemeMode;

            // Apply theme at application level for proper resource resolution and system integration
            // This ensures new windows inherit the correct theme and theme resources work properly
            var appTheme = requestedThemeMode == ElementTheme.Light
                ? ApplicationTheme.Light
                : ApplicationTheme.Dark;

            // Note: Application.RequestedTheme is read-only after app starts, but we set it on content
            // to ensure theme resources resolve correctly throughout the visual tree
            if (Application.Current.Resources.TryGetValue("AppTheme", out var existingTheme))
            {
                Application.Current.Resources["AppTheme"] = requestedThemeMode;
            }
            else
            {
                Application.Current.Resources.Add("AppTheme", requestedThemeMode);
            }

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
            this.LogFailedToChangeThemeMode(ex);
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
    ///     Ensures that theme transitions are enabled on the specified element for smooth visual changes.
    /// </summary>
    /// <param name="element">The framework element to enable transitions on.</param>
    private static void EnsureThemeTransitions(FrameworkElement element)
    {
        // Check if transitions already exist to avoid duplicates
        if (element.Transitions?.Count > 0)
        {
            // Check if we already have a theme transition
            foreach (var transition in element.Transitions)
            {
                if (transition is EntranceThemeTransition)
                {
                    return; // Transitions already configured
                }
            }
        }

        // Add smooth theme transitions
        element.Transitions ??= [];

        // EntranceThemeTransition provides smooth fade-in effect when theme changes
        element.Transitions.Add(new EntranceThemeTransition
        {
            IsStaggeringEnabled = false,
        });
    }

    /// <summary>
    ///     Applies the dark theme to the title bar of the specified window.
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
    ///     Applies the light theme to the title bar of the specified window.
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
    ///     Handles changes to the <see cref="IAppearanceSettings.AppThemeMode"/> property to update the theme mode when the setting changes.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void AppearanceSettings_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal) == true)
        {
            this.LogThemeModeSettingChanged();
        }
    }
}
