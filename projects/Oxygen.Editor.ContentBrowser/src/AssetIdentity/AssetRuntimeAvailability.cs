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
    /// Runtime availability is not known in this milestone.
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
}
