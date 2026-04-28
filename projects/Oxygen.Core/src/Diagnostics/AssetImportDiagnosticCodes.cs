// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for asset import execution.
/// </summary>
public static class AssetImportDiagnosticCodes
{
    /// <summary>
    /// Import source path is missing.
    /// </summary>
    public const string SourceMissing = DiagnosticCodes.AssetImportPrefix + "SourceMissing";

    /// <summary>
    /// Import execution failed.
    /// </summary>
    public const string ImportFailed = DiagnosticCodes.AssetImportPrefix + "ImportFailed";
}
