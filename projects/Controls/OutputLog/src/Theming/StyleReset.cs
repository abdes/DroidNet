// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Provides a mechanism to apply and reset styles for a text run within a container.
/// </summary>
/// <param name="theme">The theme to apply to the run.</param>
/// <param name="container">The container to which the run belongs.</param>
/// <param name="run">The run to style and reset.</param>
/// <remarks>
/// <para>
/// This struct is used to apply a style to a text run and ensure that the style is reset when the struct is disposed.
/// It ensures that the text run is styled consistently according to the specified theme and that the style is properly reset.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this struct to apply and reset styles for text runs in rich text controls. This can be useful for
/// applying consistent theming to log messages or other text content.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// using (var styleReset = new StyleReset(theme, paragraph, run))
/// {
///     run.Text = "Information: Operation completed successfully.";
///     // The run is styled with the specified theme
/// }
/// // The style is reset when the using block is exited
/// ]]></code>
/// </para>
/// </remarks>
internal readonly struct StyleReset(Theme theme, dynamic container, Run run) : IDisposable
{
    /// <summary>
    /// Gets the run that is being styled and reset.
    /// </summary>
    public Run Run => run;

    /// <summary>
    /// Resets the style of the run and adds it to the container.
    /// </summary>
    /// <remarks>
    /// <para>
    /// This method is called when the struct is disposed. It ensures that the run is added to the container
    /// and that the style is reset according to the specified theme.
    /// </para>
    /// </remarks>
    public void Dispose() => theme.Reset(container, run);
}
