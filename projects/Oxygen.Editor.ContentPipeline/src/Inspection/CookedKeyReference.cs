// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A native key reference; asset- and texture-target references are cook dependencies.</summary>
/// <param name="AssetKey">The referenced native key.</param>
/// <param name="TargetKind">The native target classification.</param>
/// <param name="ExpectedAssetType">The expected asset type, or zero when not applicable.</param>
public sealed record CookedKeyReference(string AssetKey, CookedKeyReferenceTargetKind TargetKind, byte ExpectedAssetType);
