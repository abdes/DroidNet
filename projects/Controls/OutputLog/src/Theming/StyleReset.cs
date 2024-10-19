// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI.Xaml.Documents;

internal readonly struct StyleReset(Theme theme, dynamic container, Run run) : IDisposable
{
    public Run Run => run;

    public void Dispose() => theme.Reset(container, run);
}
