// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Relocation;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
public sealed partial class AssetRedirects
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Resolved the reference '{Captured}', captured before a rename or move, to its current identity '{Current}'.")]
    private partial void LogRedirected(string captured, string current);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "The project changed; forgot {Count} recorded relocations and deletions.")]
    private static partial void LogCleared(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogCleared(int count)
        => LogCleared(this.logger, count);
}
