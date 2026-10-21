// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <content>Logging for <see cref="DocumentArea"/>.</content>
public sealed partial class DocumentArea
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Closed the {Kind} document '{Title}' with its tab; it had unsaved changes: {Dirty}.")]
    private static partial void LogDocumentClosed(ILogger logger, string kind, string title, bool dirty);

    private void LogDocumentClosed(string kind, string title, bool dirty)
        => LogDocumentClosed(this.logger, kind, title, dirty);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The window refused a tab for '{Title}'.")]
    private static partial void LogShowRefused(ILogger logger, string title);

    private void LogShowRefused(string title) => LogShowRefused(this.logger, title);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{Asset}' was not opened: {Problem}")]
    private static partial void LogNotOpened(ILogger logger, AssetPath asset, string problem);

    private void LogNotOpened(AssetPath asset, string problem) => LogNotOpened(this.logger, asset, problem);
}
