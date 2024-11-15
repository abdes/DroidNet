// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Settings;

using Microsoft.UI.Xaml;

/// <summary>
/// Represents the appearance settings for an application.
/// </summary>
public interface IAppearanceSettings
{
    /// <summary>
    /// Gets or sets a value for the app theme mode.
    /// </summary>
    /// <value>
    /// The app theme mode, which determines the overall theme (light, dark, or default) of the application.
    /// </value>
    ElementTheme AppThemeMode { get; set; }

    /// <summary>
    /// Gets or sets a value for the app theme background color.
    /// </summary>
    /// <value>
    /// The background color used for the application theme, specified as a hexadecimal color string.
    /// </value>
    string AppThemeBackgroundColor { get; set; }

    /// <summary>
    /// Gets or sets a value for the app theme font family.
    /// </summary>
    /// <value>
    /// The font family used for the application theme, specified as a font name string.
    /// </value>
    string AppThemeFontFamily { get; set; }
}
