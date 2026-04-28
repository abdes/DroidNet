// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
    ContentCookInputRole Role);
