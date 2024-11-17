// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Represents a theme that applies rich text styling to the rendered output.
/// </summary>
/// <param name="styles">A dictionary mapping <see cref="ThemeStyle"/> values to <see cref="RunStyle"/> instances.</param>
/// <remarks>
/// <para>
/// This class is used to apply rich text styling to the rendered output based on predefined or custom styles.
/// It ensures that the content is styled consistently according to the specified theme.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this theme when you want to apply rich text styling to the rendered output. This can be useful for
/// enhancing the readability and visual appeal of log messages.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var styles = new Dictionary<ThemeStyle, RunStyle>
/// {
///     { ThemeStyle.Text, new RunStyle { Foreground = new SolidColorBrush(Colors.White) } },
///     { ThemeStyle.LevelError, new RunStyle { Foreground = new SolidColorBrush(Colors.Red) } }
/// };
/// var theme = new RichTextTheme(styles);
/// var run = new Run { Text = "Error: Something went wrong!" };
/// theme.Apply(paragraph, ThemeStyle.LevelError);
/// // The run is added to the paragraph with the specified error styling
/// ]]></code>
/// </para>
/// </remarks>
public class RichTextTheme(IReadOnlyDictionary<ThemeStyle, RunStyle> styles) : Theme
{
    /// <summary>
    /// Resets the specified run and adds it to the container with the appropriate styling.
    /// </summary>
    /// <param name="container">The container to add the run to.</param>
    /// <param name="run">The run to reset and add to the container.</param>
    /// <remarks>
    /// <para>
    /// This method ensures that the run is added to the container with the appropriate styling.
    /// </para>
    /// </remarks>
    public override void Reset(dynamic container, Run run) => container.Inlines.Add(run);

    /// <summary>
    /// Configures the specified run with the given style.
    /// </summary>
    /// <param name="run">The run to configure.</param>
    /// <param name="style">The style to apply.</param>
    /// <remarks>
    /// <para>
    /// This method applies the specified style to the run, setting properties such as foreground color,
    /// font weight, and font style.
    /// </para>
    /// <para>
    /// <strong>Corner Cases:</strong>
    /// If the style is not found in the dictionary, no styling is applied.
    /// </para>
    /// </remarks>
    protected override void ConfigureRun(Run run, ThemeStyle style)
    {
        if (!styles.TryGetValue(style, out var runStyle))
        {
            return;
        }

        run.Foreground = runStyle.Foreground;
        run.FontWeight = runStyle.FontWeight;
        run.FontStyle = runStyle.FontStyle;
    }
}
