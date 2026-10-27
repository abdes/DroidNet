// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>Logging for <see cref="ModelPreviewFiles"/>.</summary>
internal sealed partial class ModelPreviewFiles
{
    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "The model preview '{Path}' could not be read; the model shows the default preview.")]
    private static partial void LogUnreadable(ILogger logger, string path, Exception ex);

    private void LogUnreadable(FileSystemPath path, Exception ex) => LogUnreadable(logger, path.FullPath, ex);

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "The model preview '{Path}' could not be written or deleted.")]
    private static partial void LogUnwritable(ILogger logger, string path, Exception ex);

    private void LogUnwritable(FileSystemPath path, Exception ex) => LogUnwritable(logger, path.FullPath, ex);
}
