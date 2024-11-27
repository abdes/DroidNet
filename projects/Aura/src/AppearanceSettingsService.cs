// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura;

/// <summary>
/// Specialized <see cref="ISettingsService{TSettings}" /> for the <see cref="AppearanceSettings" />
/// configuration. Provides intuitive access to the settings through properties with change notifications.
/// </summary>
public sealed partial class AppearanceSettingsService : SettingsService<AppearanceSettings>, IAppearanceSettings
{
    private readonly string configFilePath;

    /// <summary>
    /// Stores the current value of the <see cref="IAppearanceSettings.AppThemeMode" /> property.
    /// </summary>
    private ElementTheme appThemeMode;

    /// <summary>
    /// Stores the current value of the <see cref="IAppearanceSettings.AppThemeBackgroundColor" /> property.
    /// </summary>
    private string appThemeBackgroundColor;

    /// <summary>
    /// Stores the current value of the <see cref="IAppearanceSettings.AppThemeFontFamily" /> property.
    /// </summary>
    private string appThemeFontFamily;

    /// <summary>
    /// Initializes a new instance of the <see cref="AppearanceSettingsService" /> class.
    /// </summary>
    /// <param name="settingsMonitor">The settings monitor.</param>
    /// <param name="fs">The file system.</param>
    /// <param name="finder">The path finder used to determine the config file path.</param>
    /// <param name="loggerFactory">
    /// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    /// cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public AppearanceSettingsService(
        IOptionsMonitor<AppearanceSettings> settingsMonitor,
        IFileSystem fs,
        IPathFinder finder,
        ILoggerFactory? loggerFactory = null)
        : base(settingsMonitor, fs, loggerFactory)
    {
        var initialSettings = settingsMonitor.CurrentValue;

        // For the initial settings, do not fire the property change event, as these will
        // simply compare default values to the values from the configuration.
        this.appThemeMode = initialSettings.AppThemeMode;
        this.appThemeBackgroundColor = initialSettings.AppThemeBackgroundColor;
        this.appThemeFontFamily = initialSettings.AppThemeFontFamily;

        // Get our config file path from the path finder
        this.configFilePath = finder.GetConfigFilePath(AppearanceSettings.ConfigFileName);
    }

    /// <inheritdoc />
    public ElementTheme AppThemeMode
    {
        get => this.appThemeMode;
        [MemberNotNull(nameof(appThemeMode))]
        set => _ = this.SetField(ref this.appThemeMode, value);
    }

    /// <inheritdoc />
    public string AppThemeBackgroundColor
    {
        get => this.appThemeBackgroundColor;
        set => _ = this.SetField(ref this.appThemeBackgroundColor, value);
    }

    /// <inheritdoc />
    public string AppThemeFontFamily
    {
        get => this.appThemeFontFamily;
        set => _ = this.SetField(ref this.appThemeFontFamily, value);
    }

    /// <summary>
    /// Gets a snapshot of the current appearance settings. Future modifications of the settings
    /// will not be reflected in the returned snapshot.
    /// </summary>
    /// <returns>A snapshot of the appearance settings.</returns>
    protected override AppearanceSettings GetSettingsSnapshot()
        => new()
        {
            AppThemeMode = this.appThemeMode,
            AppThemeBackgroundColor = this.appThemeBackgroundColor,
            AppThemeFontFamily = this.appThemeFontFamily,
        };

    /// <summary>
    /// Updates the properties of the <see cref="AppearanceSettings" /> with new values, and fires
    /// property change notifications if the new value is different from the current one..
    /// </summary>
    /// <param name="newSettings">The new settings to apply.</param>
    protected override void UpdateProperties(AppearanceSettings newSettings)
    {
        this.AppThemeMode = newSettings.AppThemeMode;
        this.AppThemeBackgroundColor = newSettings.AppThemeBackgroundColor;
        this.AppThemeFontFamily = newSettings.AppThemeFontFamily;
    }

    /// <inheritdoc />
    protected override string GetConfigFilePath() => this.configFilePath;

    /// <inheritdoc />
    protected override string GetConfigSectionName() => AppearanceSettings.ConfigSectionName;
}
