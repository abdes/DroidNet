// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI.Xaml.Documents;

/// <summary>
/// The base class for styled terminal output.
/// </summary>
public abstract class Theme
{
    /// <summary>
    /// Gets a Them does not apply any styles.
    /// </summary>
    public static Theme None { get; } = new EmptyTheme();

    public abstract void Reset(dynamic container, Run run);

    internal StyleReset Apply(Paragraph paragraph, ThemeStyle style)
    {
        var run = new Run();
        this.ConfigureRun(run, style);
        return new StyleReset(this, paragraph, run);
    }

    internal StyleReset Apply(Span span, ThemeStyle style)
    {
        var run = new Run();
        this.ConfigureRun(run, style);
        return new StyleReset(this, span, run);
    }

    protected abstract void ConfigureRun(Run run, ThemeStyle style);
}
