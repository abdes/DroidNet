// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cooked asset entry returned by cooked-output inspection.
/// </summary>
/// <param name="VirtualPath">The native asset virtual path.</param>
/// <param name="Kind">The asset kind.</param>
public sealed record CookedAssetEntry(string VirtualPath, ContentCookAssetKind Kind)
{
    /// <summary>Gets the descriptor's physical path within the inspected root, as recorded by the native index.</summary>
    public string? DescriptorRelativePath { get; init; }
}
