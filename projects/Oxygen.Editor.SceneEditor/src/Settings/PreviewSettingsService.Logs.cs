// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <content>Logging for preview preference persistence.</content>
public sealed partial class PreviewSettingsService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Preview preferences failed ({Code}) for project {ProjectRoot}.")]
    private static partial void LogPreferenceFailure(ILogger logger, Exception exception, string code, FileSystemPath projectRoot);

    private void LogPreferenceFailure(Exception exception, string code, ProjectContext project)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var root = project.ProjectRoot;
        LogPreferenceFailure(this.logger, exception, code, root);
    }
}
