// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for runtime cooked-root mount workflows.
/// </summary>
public static class AssetMountDiagnosticCodes
{
    /// <summary>
    /// Runtime cooked-root refresh failed.
    /// </summary>
    public const string RefreshFailed = DiagnosticCodes.AssetMountPrefix + "RefreshFailed";
}
