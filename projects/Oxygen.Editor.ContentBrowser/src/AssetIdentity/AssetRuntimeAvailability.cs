// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Runtime availability overlay for an asset row.
/// </summary>
public enum AssetRuntimeAvailability
{
    /// <summary>
    /// Runtime availability does not apply to this row.
    /// </summary>
    NotApplicable,

    /// <summary>
    /// Runtime availability has not been established.
    /// </summary>
    Unknown,

    /// <summary>
    /// Asset is not known to be mounted.
    /// </summary>
    NotMounted,

    /// <summary>
    /// Asset is mounted in the runtime.
    /// </summary>
    Mounted,

    /// <summary>The native preview is not running or is incompatible.</summary>
    Unavailable,

    /// <summary>Native roots or current asset bindings are being updated.</summary>
    Updating,

    /// <summary>A current native load or application failed.</summary>
    Failed,
}
