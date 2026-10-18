// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>Logging methods for <see cref="OpenDocuments"/>.</summary>
public sealed partial class OpenDocuments
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Opened {Kind} '{Title}' from '{Path}'.")]
    private static partial void LogOpened(ILogger logger, string kind, string title, FileSystemPath path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not open '{Path}': {Problem}")]
    private static partial void LogOpenFailed(ILogger logger, FileSystemPath path, string problem);

    [LoggerMessage(Level = LogLevel.Information, Message = "Closed {Kind} '{Title}'; unsaved edits discarded: {Discarded}.")]
    private static partial void LogClosed(ILogger logger, string kind, string title, bool discarded);

    private void LogOpened(string kind, string title, FileSystemPath path) => LogOpened(this.logger, kind, title, path);

    private void LogOpenFailed(FileSystemPath path, string problem) => LogOpenFailed(this.logger, path, problem);

    private void LogClosed(string kind, string title, bool discarded) => LogClosed(this.logger, kind, title, discarded);
}
