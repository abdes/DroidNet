// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Provides predefined rich text themes for styling log output.
/// </summary>
/// <remarks>
/// <para>
/// This class contains static properties that define various rich text themes. Each theme is represented
/// by a <see cref="RichTextTheme"/> instance, which maps <see cref="ThemeStyle"/> values to <see cref="RunStyle"/> instances.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use these predefined themes to apply consistent styling to log output. You can also create custom themes
/// by defining your own mappings of <see cref="ThemeStyle"/> to <see cref="RunStyle"/>.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var theme = RichTextThemeDefinitions.Literate;
/// var run = new Run { Text = "Information: Operation completed successfully." };
/// theme.Apply(paragraph, ThemeStyle.Information);
/// // The run is added to the paragraph with the specified information styling
/// ]]></code>
/// </para>
/// </remarks>
internal static class RichTextThemeDefinitions
{
    /// <summary>
    /// Gets the "Literate" theme, which uses a light-on-dark color scheme.
    /// </summary>
    public static RichTextTheme Literate { get; } = new(
        new Dictionary<ThemeStyle, RunStyle>
        {
            [ThemeStyle.Text] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.SecondaryText] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.TertiaryText] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.Invalid] = new() { Foreground = new SolidColorBrush(Colors.Yellow) },
            [ThemeStyle.Null] = new() { Foreground = new SolidColorBrush(Colors.Blue) },
            [ThemeStyle.Name] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.String] = new() { Foreground = new SolidColorBrush(Colors.Cyan) },
            [ThemeStyle.Number] = new() { Foreground = new SolidColorBrush(Colors.Magenta) },
            [ThemeStyle.Boolean] = new() { Foreground = new SolidColorBrush(Colors.Blue) },
            [ThemeStyle.Scalar] = new() { Foreground = new SolidColorBrush(Colors.Green) },
            [ThemeStyle.LevelVerbose] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.LevelDebug] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.LevelInformation] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.LevelWarning] = new() { Foreground = new SolidColorBrush(Colors.Yellow) },
            [ThemeStyle.LevelError] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.Red),
            },
            [ThemeStyle.LevelFatal] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.Red),
            },
        });

    /// <summary>
    /// Gets the "Grayscale" theme, which uses shades of gray for styling.
    /// </summary>
    public static RichTextTheme Grayscale { get; } = new(
        new Dictionary<ThemeStyle, RunStyle>
        {
            [ThemeStyle.Text] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.SecondaryText] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.TertiaryText] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.Invalid] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.DarkGray),
            },
            [ThemeStyle.Null] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Name] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.String] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Number] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Boolean] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Scalar] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.LevelVerbose] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.LevelDebug] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.LevelInformation] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.LevelWarning] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.DarkGray),
            },
            [ThemeStyle.LevelError] = new()
            {
                Foreground = new SolidColorBrush(Colors.Black),
                Background = new SolidColorBrush(Colors.White),
            },
            [ThemeStyle.LevelFatal] = new()
            {
                Foreground = new SolidColorBrush(Colors.Black),
                Background = new SolidColorBrush(Colors.White),
            },
        });

    /// <summary>
    /// Gets the "Colored" theme, which uses a colorful palette for styling.
    /// </summary>
    public static RichTextTheme Colored { get; } = new(
        new Dictionary<ThemeStyle, RunStyle>
        {
            [ThemeStyle.Text] = new() { Foreground = new SolidColorBrush(Colors.Gray) },
            [ThemeStyle.SecondaryText] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.TertiaryText] = new() { Foreground = new SolidColorBrush(Colors.DarkGray) },
            [ThemeStyle.Invalid] = new() { Foreground = new SolidColorBrush(Colors.Yellow) },
            [ThemeStyle.Null] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Name] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.String] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Number] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Boolean] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.Scalar] = new() { Foreground = new SolidColorBrush(Colors.White) },
            [ThemeStyle.LevelVerbose] = new()
            {
                Foreground = new SolidColorBrush(Colors.Gray),
                Background = new SolidColorBrush(Colors.DarkGray),
            },
            [ThemeStyle.LevelDebug] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.DarkGray),
            },
            [ThemeStyle.LevelInformation] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.Blue),
            },
            [ThemeStyle.LevelWarning] = new()
            {
                Foreground = new SolidColorBrush(Colors.DarkGray),
                Background = new SolidColorBrush(Colors.Yellow),
            },
            [ThemeStyle.LevelError] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.Red),
            },
            [ThemeStyle.LevelFatal] = new()
            {
                Foreground = new SolidColorBrush(Colors.White),
                Background = new SolidColorBrush(Colors.Red),
            },
        });
}
