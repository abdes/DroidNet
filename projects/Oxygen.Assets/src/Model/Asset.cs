// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets;

/// <summary>
/// Abstract base class for all asset types in the editor.
/// </summary>
/// <remarks>
/// Assets represent metadata about resources (meshes, materials, textures, etc.) that can be referenced
/// and loaded by the editor. Each asset has a unique URI that follows the format:
/// <c>asset://{MountPoint}/{Path}</c>.
/// </remarks>
public abstract class Asset
{
    /// <summary>
    /// Gets or sets the canonical URI for this asset.
    /// </summary>
    /// <value>
    /// The asset URI in the format <c>asset://{MountPoint}/{Path}</c>.
    /// For example: <c>asset://Generated/BasicShapes/Cube</c> or <c>asset://Content/Models/Hero.geo</c>.
    /// </value>
    public required Uri Uri { get; set; }

    /// <summary>
    /// Gets the name of the asset extracted from the URI.
    /// </summary>
    /// <value>
    /// The filename without extension, extracted from the last segment of the URI path.
    /// </value>
    public string Name => Path.GetFileNameWithoutExtension(this.Uri.AbsolutePath);
}
