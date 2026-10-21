// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Projects;

/// <summary>Log messages.</summary>
public partial class ProjectManagerService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not save project info to `{Location}`; {Error}")]
    private static partial void CouldNotSaveProjectInfo(ILogger logger, string location, string error);

    private void CouldNotSaveProjectInfo(string location, string error)
        => CouldNotSaveProjectInfo(this.logger, location, error);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not load project info from `{Location}`; {Error}")]
    private static partial void CouldNotLoadProjectInfo(ILogger logger, string location, string error);

    private void CouldNotLoadProjectInfo(string location, string error)
        => CouldNotLoadProjectInfo(this.logger, location, error);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not load project from `{Location}`; {Error}")]
    private static partial void CouldNotLoadProject(ILogger logger, string location, string error);

    private void CouldNotLoadProject(string location, string error)
        => CouldNotLoadProject(this.logger, location, error);
}
