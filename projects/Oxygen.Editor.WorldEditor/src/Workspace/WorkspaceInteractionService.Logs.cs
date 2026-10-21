// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Logging helpers for <see cref="WorkspaceInteractionService"/>.</summary>
public sealed partial class WorkspaceInteractionService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Scene Explorer workspace state: {NodeId} {Change}.")]
    private static partial void LogStateChange(ILogger logger, NodeId nodeId, string change);

    private void LogStateChange(NodeId nodeId, bool requested, string appliedMessage, string reversedMessage)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var change = requested ? appliedMessage : reversedMessage;
        LogStateChange(this.logger, nodeId, change);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Workspace interaction state failed ({Code}) for project {ProjectRoot}.")]
    private static partial void LogInteractionFailure(ILogger logger, Exception exception, string code, FileSystemPath projectRoot);

    private void LogInteractionFailure(Exception exception, string code, FileSystemPath projectRoot)
        => LogInteractionFailure(this.logger, exception, code, projectRoot);
}
