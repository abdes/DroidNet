// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Textures;

/// <summary>A user's reviewed image-to-texture import request.</summary>
/// <param name="Project">The project whose authoring mount receives the texture.</param>
/// <param name="SourcePath">The selected image file.</param>
/// <param name="DestinationFolder">The selected authoring folder URI.</param>
/// <param name="Name">The unique texture asset name without file extensions, as typed; it is kept trimmed and in NFC.</param>
/// <param name="Intent">The native texture semantic.</param>
/// <param name="ColorSpace">The native decode color space.</param>
/// <param name="Format">The native output format.</param>
public sealed record TextureImportRequest(
    ProjectContext Project,
    string SourcePath,
    AssetPath DestinationFolder,
    string Name,
    string Intent,
    string ColorSpace,
    string Format)
{
    private readonly string name = VirtualPath.NormalizeTypedName(Name);

    /// <summary>Gets the texture asset name, trimmed and in NFC as the virtual path rules require.</summary>
    public string Name
    {
        get => this.name;
        init => this.name = VirtualPath.NormalizeTypedName(value);
    }

    /// <summary>Gets how the image becomes a cube texture, or null for a 2D texture.</summary>
    public TextureCubeImport? Cube { get; init; }
}
