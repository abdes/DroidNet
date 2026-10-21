// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Logging helpers for <see cref="VirtualFolderMountTreeItemAdapter"/>.</summary>
public sealed partial class VirtualFolderMountTreeItemAdapter
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while loading folders from mount root `{location}`: {error}")]
    private static partial void CouldNotLoadMountFolders(ILogger logger, string location, string error);

    private void CouldNotLoadMountFolders(string location, Exception exception)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        CouldNotLoadMountFolders(this.logger, location, exception.Message);
    }
}
