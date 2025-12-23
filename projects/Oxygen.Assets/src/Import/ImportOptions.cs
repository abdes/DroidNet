// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Options controlling import behavior.
/// </summary>
/// <param name="ReimportIfUnchanged">If true, forces re-import even when inputs appear unchanged.</param>
/// <param name="FailFast">If true, stops on first error diagnostic.</param>
/// <param name="Progress">Optional progress reporting callback.</param>
/// <param name="LogLevel">Controls the verbosity of collected diagnostics.</param>
public sealed record ImportOptions(
    bool ReimportIfUnchanged = false,
    bool FailFast = false,
    IProgress<ImportProgress>? Progress = null,
    ImportLogLevel LogLevel = ImportLogLevel.Info);
