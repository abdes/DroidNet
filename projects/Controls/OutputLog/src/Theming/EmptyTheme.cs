// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI.Xaml.Documents;

internal sealed class EmptyTheme : Theme
{
    public override void Reset(dynamic container, Run run)
    {
    }

    protected override void ConfigureRun(Run run, ThemeStyle style)
    {
    }
}
