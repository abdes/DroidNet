// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI;
using Microsoft.UI.Xaml.Media;

internal static class RichTextThemeDefinitions
{
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
