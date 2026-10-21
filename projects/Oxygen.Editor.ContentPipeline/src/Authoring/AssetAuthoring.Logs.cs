// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>Logging helpers for <see cref="AssetAuthoring"/>.</summary>
/// <remarks>Levels: Information for each file created; Warning for a file not created and why.</remarks>
public sealed partial class AssetAuthoring
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created '{Path}' from its template.")]
    private static partial void LogCreated(ILogger logger, FileSystemPath path);

    private void LogCreated(FileSystemPath path) => LogCreated(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{Name}' was not created in {Folder}: {Reason}")]
    private static partial void LogNotCreated(ILogger logger, string name, AssetPath folder, string reason);

    private void LogNotCreated(string name, AssetPath folder, string reason) => LogNotCreated(this.logger, name, folder, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not write the new file '{Path}'.")]
    private static partial void LogWriteFailed(ILogger logger, Exception exception, FileSystemPath path);

    private void LogWriteFailed(Exception exception, FileSystemPath path) => LogWriteFailed(this.logger, exception, path);
}
