// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Logging helpers for <see cref="AssetRedirects"/>.</summary>
public sealed partial class AssetRedirects
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Resolved the reference '{Captured}', captured before a rename or move, to its current identity '{Current}'.")]
    private static partial void LogRedirected(ILogger logger, AssetPath captured, AssetPath current);

    private void LogRedirected(AssetPath captured, AssetPath current) => LogRedirected(this.logger, captured, current);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The project changed; forgot {Count} recorded relocations and deletions.")]
    private static partial void LogCleared(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogCleared(int count) => LogCleared(this.logger, count);
}
