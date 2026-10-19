// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>How the cooker makes a cube texture from a texture's images, as the descriptor's <c>cube</c> states it.</summary>
public sealed record TextureCubeSettings
{
    /// <summary>Gets whether the images are cube faces: six named files, or one image laid out as <see cref="Layout"/>.</summary>
    public bool? Cubemap { get; init; }

    /// <summary>Gets whether the cooker projects an equirectangular panorama onto a cube.</summary>
    public bool? EquirectToCube { get; init; }

    /// <summary>Gets the size of each face the panorama projects to, in pixels.</summary>
    public int? FaceSize { get; init; }

    /// <summary>Gets how one image lays out the six faces: <c>auto</c>, <c>hstrip</c>, <c>vstrip</c>, <c>hcross</c> or
    /// <c>vcross</c>.</summary>
    public string? Layout { get; init; }

    /// <summary>Gets whether these settings make a cube texture.</summary>
    internal bool MakesCube => this.Cubemap == true || this.EquirectToCube == true || this.Layout is not null;
}
