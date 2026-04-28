// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One asset produced by a content cook workflow.
/// </summary>
/// <param name="SourceAssetUri">The authored source asset URI.</param>
/// <param name="CookedAssetUri">The cooked asset URI.</param>
/// <param name="Kind">The asset kind.</param>
/// <param name="MountName">The mount that owns the cooked output.</param>
/// <param name="VirtualPath">The native cooked virtual path.</param>
public sealed record ContentCookedAsset(
    Uri SourceAssetUri,
    Uri CookedAssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string VirtualPath);
