// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using Microsoft.UI.Xaml.Documents;
using Serilog.Events;

public interface ILogEventRenderer
{
    void Render(LogEvent logEvent, Paragraph paragraph);
}
