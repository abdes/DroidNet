// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura;

/// <summary>
/// Implements the <see cref="IAppearanceSettings" /> interface to provide appearance settings for an application.
/// </summary>
public class AppearanceSettings : IAppearanceSettings
{
    /// <summary>
    /// The name of the configuration file where the appearance settings are stored.
    /// </summary>
    public const string ConfigFileName = "LocalSettings.json";

    /// <summary>
    /// The name of the configuration section that contains the appearance settings.
    /// </summary>
    public const string ConfigSectionName = "AppearanceSettings";

    /// <summary>
    /// Gets or sets a value for the app theme mode.
    /// </summary>
    /// <value>
    /// The app theme mode, which determines the overall theme (light, dark, or default) of the application.
    /// </value>
    public ElementTheme AppThemeMode { get; set; } = ElementTheme.Default;

    /// <summary>
    /// Gets or sets a value for the app theme background color.
    /// </summary>
    /// <value>
    /// The background color used for the application theme, specified as a hexadecimal color string.
    /// </value>
    public string AppThemeBackgroundColor { get; set; } = "#00000000";

    /// <summary>
    /// Gets or sets a value for the app theme font family.
    /// </summary>
    /// <value>
    /// The font family used for the application theme, specified as a font name string.
    /// </value>
    public string AppThemeFontFamily { get; set; } = "Segoe UI Variable";
}
