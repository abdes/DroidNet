// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Represents the base class for styled terminal output.
/// </summary>
/// <remarks>
/// <para>
/// This abstract class provides the foundation for creating themes that apply consistent styling to text runs
/// in rich text controls. Derived classes must implement the <see cref="ConfigureRun"/> method to define how
/// styles are applied to text runs.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this class as a base for creating custom themes that apply specific styles to text runs. This can be useful
/// for enhancing the readability and visual appeal of log messages or other text content.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// public class CustomTheme : Theme
/// {
///     protected override void ConfigureRun(Run run, ThemeStyle style)
///     {
///         switch (style)
///         {
///             case ThemeStyle.Text:
///                 run.Foreground = new SolidColorBrush(Colors.White);
///                 run.FontWeight = FontWeights.Normal;
///                 break;
///             case ThemeStyle.LevelError:
///                 run.Foreground = new SolidColorBrush(Colors.Red);
///                 run.FontWeight = FontWeights.Bold;
///                 break;
///             // Add more styles as needed
///         }
///     }
/// }
/// ]]></code>
/// </para>
/// </remarks>
public abstract class Theme
{
    /// <summary>
    /// Gets a theme that does not apply any styles.
    /// </summary>
    public static Theme None { get; } = new EmptyTheme();

    /// <summary>
    /// Resets the specified run and adds it to the container without applying any styles.
    /// </summary>
    /// <param name="container">The container to add the run to.</param>
    /// <param name="run">The run to reset and add to the container.</param>
    /// <remarks>
    /// <para>
    /// This method ensures that the run is added to the container without any additional styling.
    /// </para>
    /// </remarks>
    public abstract void Reset(dynamic container, Run run);

    /// <summary>
    /// Applies the specified style to a run within a paragraph.
    /// </summary>
    /// <param name="paragraph">The paragraph to add the run to.</param>
    /// <param name="style">The style to apply to the run.</param>
    /// <returns>A <see cref="StyleReset"/> instance that ensures the style is reset when disposed.</returns>
    /// <remarks>
    /// <para>
    /// This method creates a new run, applies the specified style using the <see cref="ConfigureRun"/> method,
    /// and adds the run to the paragraph. The style is reset when the returned <see cref="StyleReset"/> instance
    /// is disposed.
    /// </para>
    /// </remarks>
    internal StyleReset Apply(Paragraph paragraph, ThemeStyle style)
    {
        var run = new Run();
        this.ConfigureRun(run, style);
        return new StyleReset(this, paragraph, run);
    }

    /// <summary>
    /// Applies the specified style to a run within a span.
    /// </summary>
    /// <param name="span">The span to add the run to.</param>
    /// <param name="style">The style to apply to the run.</param>
    /// <returns>A <see cref="StyleReset"/> instance that ensures the style is reset when disposed.</returns>
    /// <remarks>
    /// <para>
    /// This method creates a new run, applies the specified style using the <see cref="ConfigureRun"/> method,
    /// and adds the run to the span. The style is reset when the returned <see cref="StyleReset"/> instance
    /// is disposed.
    /// </para>
    /// </remarks>
    internal StyleReset Apply(Span span, ThemeStyle style)
    {
        var run = new Run();
        this.ConfigureRun(run, style);
        return new StyleReset(this, span, run);
    }

    /// <summary>
    /// Configures the specified run with the given style.
    /// </summary>
    /// <param name="run">The run to configure.</param>
    /// <param name="style">The style to apply.</param>
    /// <remarks>
    /// <para>
    /// Derived classes must implement this method to define how styles are applied to text runs.
    /// </para>
    /// </remarks>
    protected abstract void ConfigureRun(Run run, ThemeStyle style);
}
