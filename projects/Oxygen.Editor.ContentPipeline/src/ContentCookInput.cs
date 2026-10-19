// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One authored or generated input included in a cook request.
/// </summary>
/// <param name="AssetUri">The authored asset URI.</param>
/// <param name="Kind">The cook asset kind.</param>
/// <param name="MountName">The authoring mount name.</param>
/// <param name="SourceRelativePath">The project-relative source path.</param>
/// <param name="SourceAbsolutePath">The absolute source path.</param>
/// <param name="Role">The input role.</param>
public sealed record ContentCookInput(
    AssetPath AssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string SourceRelativePath,
    FileSystemPath SourceAbsolutePath,
    ContentCookInputRole Role)
{
    /// <summary>Tests, before cooking, whether this input will produce an asset.</summary>
    /// <remarks>An input file's actions and contexts are known only from the provenance of its last cook.</remarks>
    /// <param name="virtualPath">The native output identity.</param>
    /// <returns>Whether this input is the asset, or the model the asset is named under.</returns>
    public bool OwnsOutput(string virtualPath) => this.Kind == ContentCookAssetKind.ForeignSource
        ? AssetPath.TryParse(virtualPath, out var output) && AssetFileTypes.ContainerOf(output) == this.AssetUri
        : string.Equals(this.AssetUri.VirtualPath, virtualPath, StringComparison.Ordinal);
}
