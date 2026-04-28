// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for asset identity and Content Browser rows.
/// </summary>
public static class AssetIdentityDiagnosticCodes
{
    /// <summary>
    /// Asset catalog query failed.
    /// </summary>
    public const string QueryFailed = DiagnosticCodes.AssetIdentityPrefix + "QueryFailed";

    /// <summary>
    /// Asset identity reduction failed.
    /// </summary>
    public const string ReduceFailed = DiagnosticCodes.AssetIdentityPrefix + "ReduceFailed";

    /// <summary>
    /// Authored asset reference did not resolve.
    /// </summary>
    public const string ResolveMissing = DiagnosticCodes.AssetIdentityPrefix + "Resolve.Missing";

    /// <summary>
    /// Descriptor exists but cannot be read or validated.
    /// </summary>
    public const string DescriptorBroken = DiagnosticCodes.AssetIdentityPrefix + "Descriptor.Broken";

    /// <summary>
    /// Expected cooked companion is missing.
    /// </summary>
    public const string CookedMissing = DiagnosticCodes.AssetIdentityPrefix + "Cooked.Missing";

    /// <summary>
    /// Post-cook asset catalog refresh failed.
    /// </summary>
    public const string RefreshFailed = DiagnosticCodes.AssetIdentityPrefix + "RefreshFailed";

    /// <summary>
    /// User selected an invalid project content root.
    /// </summary>
    public const string ContentRootInvalidSelection = DiagnosticCodes.ProjectPrefix + "CONTENT_ROOT.InvalidSelection";

    /// <summary>
    /// Cooked asset is older than its descriptor/source.
    /// </summary>
    public const string AssetStale = DiagnosticCodes.ContentPipelinePrefix + "Asset.Stale";
}
