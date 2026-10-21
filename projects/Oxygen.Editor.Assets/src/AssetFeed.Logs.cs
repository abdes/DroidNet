// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Assets;

/// <summary>Logging helpers for <see cref="AssetFeed"/>.</summary>
public sealed partial class AssetFeed
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unable to refresh asset status for the Content Explorer.")]
    private static partial void LogRefreshFailed(ILogger logger, Exception exception);

    private void LogRefreshFailed(Exception exception) => LogRefreshFailed(this.logger, exception);
}
