// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Logging helpers for <see cref="AssetCookStatusReader"/>.</summary>
public sealed partial class AssetCookStatusReader
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Read the cook status of {Assets} assets over {Files} input files in {Total} ms: inputs {Inputs} ms, libraries {Libraries} ms, dependencies {Dependencies} ms, freshness {Freshness} ms, input hashes {Hashes} ms.")]
    private static partial void LogReadTimes(ILogger logger, int assets, int files, long total, long inputs, long libraries, long dependencies, long freshness, long hashes);

    private void LogReadTimes(ReadTimes times, int assets, int files)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var total = (long)Stopwatch.GetElapsedTime(times.Started, times.Finished).TotalMilliseconds;
        var inputs = (long)Stopwatch.GetElapsedTime(times.Started, times.InputsResolved).TotalMilliseconds;
        var libraries = (long)Stopwatch.GetElapsedTime(times.InputsResolved, times.LibrariesRead).TotalMilliseconds;
        var dependencies = (long)Stopwatch.GetElapsedTime(times.LibrariesRead, times.Discovered).TotalMilliseconds;
        var freshness = (long)Stopwatch.GetElapsedTime(times.Discovered, times.FreshnessRead).TotalMilliseconds;
        var hashes = (long)Stopwatch.GetElapsedTime(times.FreshnessRead, times.Finished).TotalMilliseconds;
        LogReadTimes(this.logger, assets, files, total, inputs, libraries, dependencies, freshness, hashes);
    }

    // The timestamps of one status read, at the end of each of its steps.
    private readonly record struct ReadTimes(long Started, long InputsResolved, long LibrariesRead, long Discovered, long FreshnessRead, long Finished);
}
