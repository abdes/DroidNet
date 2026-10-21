// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Logging methods for <see cref="SceneReferencesSectionViewModel"/>.</summary>
public sealed partial class SceneReferencesSectionViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared content catalog could not be refreshed for the scene references.")]
    private static partial void LogCatalogRefreshFailed(ILogger logger, Exception exception);

    private void LogCatalogRefreshFailed(Exception exception) => LogCatalogRefreshFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared content catalog feed failed; the scene references stop following it.")]
    private static partial void LogCatalogFeedFailed(ILogger logger, Exception exception);

    private void LogCatalogFeedFailed(Exception exception) => LogCatalogFeedFailed(this.logger, exception);
}
