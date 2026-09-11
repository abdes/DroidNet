// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>One source dependency discovered before coherent capture.</summary>
/// <param name="AssetUri">The logical authored identity, when this file is an asset.</param>
/// <param name="SourcePath">The original absolute source path.</param>
/// <param name="RelativePath">The preserved path inside the private input root.</param>
/// <param name="DiscoveryHash">The hash observed while resolving dependencies.</param>
public sealed record CookSnapshotInput(Uri? AssetUri, string SourcePath, string RelativePath, string DiscoveryHash);
