// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ModelEditor;

/// <summary>Logging for <see cref="ModelEditorLayout"/>.</summary>
public sealed partial class ModelEditorLayout
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "The Model Editor layout could not be read; the defaults apply")]
    private static partial void LogUnreadable(ILogger logger, Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The Model Editor layout could not be saved")]
    private static partial void LogWriteFailed(ILogger logger, Exception exception);

    private void LogUnreadable(Exception exception) => LogUnreadable(this.logger, exception);

    private void LogWriteFailed(Exception exception) => LogWriteFailed(this.logger, exception);
}
