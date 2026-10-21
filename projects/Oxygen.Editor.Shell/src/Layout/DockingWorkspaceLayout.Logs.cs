// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>
///     Logging helpers for <see cref="DockingWorkspaceLayout" />.
/// </summary>
public sealed partial class DockingWorkspaceLayout
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Syncing docker workspace layout with the router...")]
    private static partial void LogSyncWithRouter(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSyncWithRouter() => LogSyncWithRouter(this.logger);
}
