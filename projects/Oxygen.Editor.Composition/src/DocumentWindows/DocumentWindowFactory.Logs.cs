// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace Oxygen.Editor.DocumentWindows;

/// <summary>Logging helpers for <see cref="DocumentWindowFactory" />.</summary>
internal sealed partial class DocumentWindowFactory
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Opened document window {Window} for the torn-out tab of '{Title}'.")]
    private static partial void LogOpened(ILogger logger, string title, ulong window);

    private void LogOpened(string title, WindowId window) => LogOpened(this.logger, title, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The document window for the torn-out tab of '{Title}' did not open; the tab goes back where it came from.")]
    private static partial void LogNotOpened(ILogger logger, string title);

    private void LogNotOpened(string title) => LogNotOpened(this.logger, title);
}
