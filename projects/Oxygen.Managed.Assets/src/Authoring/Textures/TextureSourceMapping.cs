// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>An image a texture adds beside its <see cref="TextureSource.Source"/>: an array layer, a mip or a volume slice.</summary>
/// <param name="File">The image, relative to the descriptor's folder or rooted.</param>
public sealed record TextureSourceMapping(string File)
{
    /// <summary>Gets the array layer or cube face the image fills.</summary>
    public int? Layer { get; init; }

    /// <summary>Gets the mip level the image fills.</summary>
    public int? Mip { get; init; }

    /// <summary>Gets the volume slice the image fills.</summary>
    public int? Slice { get; init; }
}
