// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI.Xaml.Documents;

public class RichTextTheme(IReadOnlyDictionary<ThemeStyle, RunStyle> styles) : Theme
{
    public override void Reset(dynamic container, Run run) => container.Inlines.Add(run);

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
