// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Workspace;

/// <content>Logging for preview preference persistence.</content>
public sealed partial class PreviewSettingsService
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Preview preferences failed ({Code}) for project {ProjectRoot}.")]
    private partial void LogPreferenceFailure(Exception exception, string code, string? projectRoot);
}
