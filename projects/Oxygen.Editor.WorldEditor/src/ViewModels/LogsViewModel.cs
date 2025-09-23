// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;

namespace Oxygen.Editor.WorldEditor.ViewModels;

/// <summary>
///     The ViewModel for managing and displaying logs in the application.
/// </summary>
public class LogsViewModel
{
    public LogsViewModel(OutputLogBuffer buffer)
    {
        this.Buffer = buffer;
    }

    public OutputLogBuffer Buffer { get; }
}
