// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>The outcome of an identified asset inside a scoped cook.</summary>
/// <param name="AssetUri">The stable source identity.</param>
/// <param name="Kind">The asset's type.</param>
/// <param name="State">Its current execution state.</param>
/// <param name="Reason">A failure or dependent-skip reason, when applicable.</param>
public sealed record CookRunAsset(Uri AssetUri, ContentCookAssetKind Kind, CookAssetState State, string? Reason = null);
