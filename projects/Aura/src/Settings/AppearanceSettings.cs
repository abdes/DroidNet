// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Settings;

/// <summary>
///     Implements the <see cref="IAppearanceSettings" /> interface to provide appearance settings for an application.
/// </summary>
public partial class AppearanceSettings : IAppearanceSettings
{
    /// <summary>
    ///     The name of the configuration section that contains the appearance settings.
    /// </summary>
    public const string ConfigSectionName = "AppearanceSettings";

    /// <summary>
    ///     Gets or sets a value for the app theme mode.
    /// </summary>
    /// <value>The app theme mode, which determines the overall theme (light, dark, or default) of the application.</value>
    public ElementTheme AppThemeMode
    {
        get => field;
        set => field = Enum.IsDefined(value) ? value : ElementTheme.Default;
    }

    /// <summary>
    ///     Gets or sets a value for the app theme background color.
    /// </summary>
    /// <value>
    ///     The background color used for the application theme, specified as a hexadecimal color string.
    ///     Valid formats are #RRGGBB or #AARRGGBB (e.g., "#00000000", "#FF0000").
    /// </value>
    /// <remarks>
    ///     If an invalid color format is provided, the default transparent color "#00000000" will be used.
    /// </remarks>
    public string AppThemeBackgroundColor
    {
        get => field;
        set => field = IsValidHexColor(value) ? value : "#00000000";
    }

    = "#00000000";

    /// <summary>
    ///     Gets or sets a value for the app theme font family.
    /// </summary>
    /// <value>The font family used for the application theme, specified as a font name string.</value>
    public string AppThemeFontFamily { get; set; } = "Segoe UI Variable";

    /// <summary>
    ///     Validates whether a string represents a valid hexadecimal color format.
    /// </summary>
    /// <param name="color">The color string to validate.</param>
    /// <returns>
    ///     <see langword="true"/> if the color is in valid hexadecimal format (#RRGGBB or #AARRGGBB);
    ///     otherwise, <see langword="false"/>.
    /// </returns>
    private static bool IsValidHexColor(string color)
    {
        if (string.IsNullOrWhiteSpace(color))
        {
            return false;
        }

        // Validates #RRGGBB or #AARRGGBB format
        return MyRegex().IsMatch(color);
    }

    [GeneratedRegex("^#[0-9A-Fa-f]{6}$|^#[0-9A-Fa-f]{8}$", RegexOptions.None, matchTimeoutMilliseconds: 100)]
    private static partial Regex MyRegex();
}
