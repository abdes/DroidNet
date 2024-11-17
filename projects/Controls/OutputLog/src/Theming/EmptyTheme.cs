// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Represents a theme that does not apply any styles to the rendered output.
/// </summary>
/// <remarks>
/// <para>
/// This class is used when no theming is required for the rendered output. It ensures that the content
/// is added to the container without any additional styling.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this theme when you want to render content without applying any styles. This can be useful for
/// debugging or when the default styling is sufficient.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var theme = new EmptyTheme();
/// var run = new Run { Text = "Hello, World!" };
/// theme.Reset(paragraph, run);
/// // The run is added to the paragraph without any additional styling
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class EmptyTheme : Theme
{
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
    public override void Reset(dynamic container, Run run) => container.Inlines.Add(run);

    /// <summary>
    /// Configures the specified run with the given style. In this implementation, no styles are applied.
    /// </summary>
    /// <param name="run">The run to configure.</param>
    /// <param name="style">The style to apply. This parameter is ignored in this implementation.</param>
    /// <remarks>
    /// <para>
    /// This method is a no-op in this implementation, as no styles are applied to the run.
    /// </para>
    /// </remarks>
    protected override void ConfigureRun(Run run, ThemeStyle style)
    {
        // No styles are applied in this theme
    }
}
