// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>
/// How a texture repeats past the edge of its coordinates, as the engine's material descriptor names it.
/// </summary>
public enum MaterialTextureAddress
{
    /// <summary>The texture repeats.</summary>
    Repeat,

    /// <summary>The texture repeats, mirrored each time.</summary>
    Mirror,

    /// <summary>The edge texel continues.</summary>
    Clamp,
}

/// <summary>How a texture is filtered between texels.</summary>
public enum MaterialTextureFilter
{
    /// <summary>The nearest texel.</summary>
    Nearest,

    /// <summary>A blend of the nearest texels.</summary>
    Linear,
}

/// <summary>How a texture is filtered between mip levels.</summary>
public enum MaterialTextureMipFilter
{
    /// <summary>The top level only.</summary>
    None,

    /// <summary>The nearest level.</summary>
    Nearest,

    /// <summary>A blend of the two nearest levels.</summary>
    Linear,
}

/// <summary>The transform of one texture's coordinates.</summary>
/// <param name="Scale">The scale.</param>
/// <param name="Offset">The offset.</param>
/// <param name="RotationRadians">The rotation, in radians.</param>
public sealed record MaterialUvTransform(Vector2 Scale, Vector2 Offset, float RotationRadians)
{
    /// <summary>Gets the transform that changes nothing, which a descriptor does not write.</summary>
    public static MaterialUvTransform Identity { get; } = new(Vector2.One, Vector2.Zero, 0.0f);
}

/// <summary>How one texture is sampled. The engine's defaults repeat, with trilinear filtering.</summary>
public sealed record MaterialTextureSampler
{
    /// <summary>Gets the sampler the engine uses when the descriptor states nothing, which a descriptor does not write.</summary>
    public static MaterialTextureSampler Default { get; } = new();

    /// <summary>Gets how the texture repeats along U.</summary>
    public MaterialTextureAddress AddressU { get; init; } = MaterialTextureAddress.Repeat;

    /// <summary>Gets how the texture repeats along V.</summary>
    public MaterialTextureAddress AddressV { get; init; } = MaterialTextureAddress.Repeat;

    /// <summary>Gets the filter used when a texel covers more than a pixel.</summary>
    public MaterialTextureFilter MagFilter { get; init; } = MaterialTextureFilter.Linear;

    /// <summary>Gets the filter used when a texel covers less than a pixel.</summary>
    public MaterialTextureFilter MinFilter { get; init; } = MaterialTextureFilter.Linear;

    /// <summary>Gets the filter between mip levels.</summary>
    public MaterialTextureMipFilter MipFilter { get; init; } = MaterialTextureMipFilter.Linear;
}

/// <summary>A texture a material channel reads, and how it reads it.</summary>
/// <param name="Texture">The texture's identity.</param>
public sealed record MaterialTextureBinding(AssetPath Texture)
{
    /// <summary>The number of UV sets a vertex holds: a binding reads set 0 or set 1.</summary>
    public const int UvSetCount = 2;

    /// <summary>Gets the UV set the channel reads.</summary>
    public int UvSet { get; init; }

    /// <summary>Gets the transform of the channel's coordinates.</summary>
    public MaterialUvTransform UvTransform { get; init; } = MaterialUvTransform.Identity;

    /// <summary>Gets how the channel samples the texture.</summary>
    public MaterialTextureSampler Sampler { get; init; } = MaterialTextureSampler.Default;
}
