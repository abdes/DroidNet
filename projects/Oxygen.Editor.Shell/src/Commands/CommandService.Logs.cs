// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>
///     Logging helpers for <see cref="CommandService"/>.
/// </summary>
public sealed partial class CommandService
{
    private static string NameOf(DependencyObject? context) => context?.GetType().Name ?? "nowhere";

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{Command} from {Context} runs in {Target}")]
    private static partial void LogHandled(ILogger logger, string command, string context, string target);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogHandled(string commandId, DependencyObject? context, object target)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var source = NameOf(context);
        var runner = target.GetType().Name;
        LogHandled(this.logger, commandId, source, runner);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{Command} from {Context} has nothing to run it")]
    private static partial void LogUnhandled(ILogger logger, string command, string context);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUnhandled(string commandId, DependencyObject? context)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var source = NameOf(context);
        LogUnhandled(this.logger, commandId, source);
    }
}
