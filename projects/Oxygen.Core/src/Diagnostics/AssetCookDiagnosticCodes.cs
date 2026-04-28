// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for asset cook execution.
/// </summary>
public static class AssetCookDiagnosticCodes
{
    /// <summary>
    /// Cook execution failed.
    /// </summary>
    public const string CookFailed = DiagnosticCodes.AssetCookPrefix + "CookFailed";

    /// <summary>
    /// Loose cooked index is missing or invalid.
    /// </summary>
    public const string IndexInvalid = DiagnosticCodes.AssetCookPrefix + "IndexInvalid";
}
