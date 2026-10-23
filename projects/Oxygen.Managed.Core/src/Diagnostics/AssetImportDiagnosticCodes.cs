// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

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

    /// <summary>
    /// A missing file the source names outside the folder its inputs are kept
    /// under is left out of them; the import proceeds without it.
    /// </summary>
    public const string MissingDependencyOmitted = DiagnosticCodes.AssetImportPrefix + "MissingDependencyOmitted";

    /// <summary>
    /// An existing file the source names lies outside the folder its inputs are
    /// kept under, so it cannot be retained with them.
    /// </summary>
    public const string DependencyOutsideSource = DiagnosticCodes.AssetImportPrefix + "DependencyOutsideSource";
}
