// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Logging helpers for <see cref="FolderTreeItemAdapter"/>.</summary>
public partial class FolderTreeItemAdapter
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private static partial void CouldNotLoadProjectFolders(ILogger logger, string location, string error);

    private void CouldNotLoadProjectFolders(string location, Exception exception)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        CouldNotLoadProjectFolders(this.logger, location, exception.Message);
    }
}
