// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One authored or generated input included in a cook request.
/// </summary>
/// <param name="AssetUri">The authored asset URI.</param>
/// <param name="Kind">The cook asset kind.</param>
/// <param name="MountName">The authoring mount name.</param>
/// <param name="SourceRelativePath">The project-relative source path.</param>
/// <param name="SourceAbsolutePath">The absolute source path.</param>
/// <param name="OutputVirtualPath">The expected native virtual output path, when known.</param>
/// <param name="Role">The input role.</param>
public sealed record ContentCookInput(
    Uri AssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string SourceRelativePath,
    string SourceAbsolutePath,
    string? OutputVirtualPath,
    ContentCookInputRole Role)
{
    /// <summary>Gets the disjoint output folders owned by an imported source.</summary>
    public ImmutableArray<string> OutputNamespaces { get; init; } = [];

    /// <summary>Tests output ownership without treating the whole mount as an imported namespace.</summary>
    /// <param name="virtualPath">The native output identity.</param>
    /// <returns>Whether this input owns the exact asset or its imported folder.</returns>
    public bool OwnsOutput(string virtualPath) => this.Kind == ContentCookAssetKind.ForeignSource
        ? this.OutputNamespaces.Any(prefix => virtualPath.StartsWith(prefix, StringComparison.Ordinal))
            || (this.OutputNamespaces.IsEmpty && this.OutputVirtualPath is { } prefix && virtualPath.StartsWith(prefix, StringComparison.Ordinal))
        : string.Equals(this.OutputVirtualPath, virtualPath, StringComparison.Ordinal);
}
