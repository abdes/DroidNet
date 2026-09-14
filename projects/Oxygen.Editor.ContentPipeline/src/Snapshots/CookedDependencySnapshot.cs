// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>A protected cooked asset selected from a declared library.</summary>
/// <param name="AssetUri">The referenced runtime identity.</param>
/// <param name="SourceName">The saved library name.</param>
/// <param name="RootPath">The selected physical library root.</param>
/// <param name="AssetKey">The native identity serialized into consumers.</param>
/// <param name="AssetType">The indexed native type.</param>
/// <param name="ContentFingerprint">The protected container's content identity.</param>
public sealed record CookedDependencySnapshot(Uri AssetUri, string SourceName, string RootPath, string AssetKey, byte AssetType, string ContentFingerprint);
