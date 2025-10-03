// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.OutputConsole.Model;
using Serilog;
using Serilog.Events;

namespace DroidNet.Controls.Demo.OutputConsole;

public partial class OutputConsoleDemoViewModel : ObservableObject
{
    public OutputLogBuffer Buffer { get; }

    public OutputConsoleDemoViewModel(OutputLogBuffer buffer)
    {
        this.Buffer = buffer;
    }

    [RelayCommand]
    private void MakeLog(string logLevel)
    {
        if (Enum.TryParse(logLevel, out LogEventLevel level))
        {
            Log.Write(level, "{Level} log message", level);
        }
    }
}
