// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable asset identity operation-kind names used by editor operation results.
/// </summary>
public static class AssetOperationKinds
{
    /// <summary>
    /// Asset browser snapshot publication.
    /// </summary>
    public const string Browse = "Asset.Browse";

    /// <summary>
    /// Asset catalog query.
    /// </summary>
    public const string Query = "Asset.Query";

    /// <summary>
    /// Asset reference resolution.
    /// </summary>
    public const string Resolve = "Asset.Resolve";

    /// <summary>
    /// Asset identity copy action.
    /// </summary>
    public const string CopyIdentity = "Asset.CopyIdentity";
}
