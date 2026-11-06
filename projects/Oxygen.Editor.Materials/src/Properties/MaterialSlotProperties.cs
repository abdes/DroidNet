// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Properties;

/// <summary>The properties of how one texture channel reads its texture, each the JSON Pointer of its field.</summary>
/// <param name="Channel">The channel, one of <see cref="MaterialSource.TextureChannels"/>.</param>
public sealed record MaterialSlotProperties(string Channel)
{
    /// <summary>Gets the UV set the channel reads.</summary>
    public PropertyId<int> UvSet { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_set");

    /// <summary>Gets the U scale.</summary>
    public PropertyId<float> ScaleU { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_transform/scale/0");

    /// <summary>Gets the V scale.</summary>
    public PropertyId<float> ScaleV { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_transform/scale/1");

    /// <summary>Gets the U offset.</summary>
    public PropertyId<float> OffsetU { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_transform/offset/0");

    /// <summary>Gets the V offset.</summary>
    public PropertyId<float> OffsetV { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_transform/offset/1");

    /// <summary>Gets the rotation, in radians.</summary>
    public PropertyId<float> RotationRadians { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/uv_transform/rotation_radians");

    /// <summary>Gets how the texture repeats along U.</summary>
    public PropertyId<MaterialTextureAddress> AddressU { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/sampler/address_u");

    /// <summary>Gets how the texture repeats along V.</summary>
    public PropertyId<MaterialTextureAddress> AddressV { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/sampler/address_v");

    /// <summary>Gets the filter used when a texel covers more than a pixel.</summary>
    public PropertyId<MaterialTextureFilter> MagFilter { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/sampler/mag_filter");

    /// <summary>Gets the filter used when a texel covers less than a pixel.</summary>
    public PropertyId<MaterialTextureFilter> MinFilter { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/sampler/min_filter");

    /// <summary>Gets the filter between mip levels.</summary>
    public PropertyId<MaterialTextureMipFilter> MipFilter { get; } = new(MaterialDescriptors.Kind, $"/textures/{Channel}/sampler/mip_filter");
}
