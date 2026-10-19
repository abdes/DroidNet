// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>
/// Authoring-time material definition parsed from <c>*.omat.json</c>.
/// </summary>
/// <remarks>
/// This model is used by import/build tooling. Runtime consumes the cooked binary <c>.omat</c> format.
/// </remarks>
public sealed record MaterialSource
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialSource"/> class, with no texture. Assign textures with
    /// <see cref="WithTextureReference"/>.
    /// </summary>
    /// <param name="name">An optional debugging/display name.</param>
    /// <param name="pbrMetallicRoughness">The PBR metallic-roughness parameters.</param>
    /// <param name="alphaMode">The alpha mode semantics.</param>
    /// <param name="alphaCutoff">The alpha cutoff used for <see cref="MaterialAlphaMode.Mask"/>.</param>
    /// <param name="doubleSided">Whether the material should be treated as double-sided.</param>
    /// <param name="emissiveColor">Linear emission colour; defaults to white.</param>
    /// <param name="emissiveIntensity">Finite HDR emission multiplier.</param>
    public MaterialSource(
        string? name,
        MaterialPbrMetallicRoughness pbrMetallicRoughness,
        MaterialAlphaMode alphaMode,
        float alphaCutoff,
        bool doubleSided,
        Vector3? emissiveColor = null,
        float emissiveIntensity = 0.0f)
    {
        ArgumentNullException.ThrowIfNull(pbrMetallicRoughness);
        this.Name = name;
        this.PbrMetallicRoughness = pbrMetallicRoughness;
        this.AlphaMode = alphaMode;
        this.AlphaCutoff = alphaCutoff;
        this.DoubleSided = doubleSided;
        this.EmissiveColor = emissiveColor ?? Vector3.One;
        this.EmissiveIntensity = emissiveIntensity;
    }

    /// <summary>Gets the texture channels of the engine's material descriptor schema.</summary>
    public static IReadOnlyList<string> TextureChannels { get; } =
    [
        "base_color", "normal", "metallic", "roughness", "ambient_occlusion", "emissive",
        "specular", "sheen_color", "clearcoat", "clearcoat_normal", "transmission", "thickness",
    ];

    /// <summary>
    /// Gets the optional debugging/display name.
    /// </summary>
    public string? Name { get; init; }

    /// <summary>
    /// Gets the PBR metallic-roughness parameters.
    /// </summary>
    public MaterialPbrMetallicRoughness PbrMetallicRoughness
    {
        get;
        init => field = value ?? throw new ArgumentNullException(nameof(value));
    }

    /// <summary>
    /// Gets the texture of each assigned channel: the one place a material holds its textures. Change it with
    /// <see cref="WithTextureReference"/>, which checks the channel; the descriptor spells each one as its virtual path.
    /// </summary>
    public IReadOnlyDictionary<string, AssetPath> TextureReferences { get; internal init; }
        = FrozenDictionary<string, AssetPath>.Empty;

    /// <summary>Gets the normal-map scale, kept whether or not a normal texture is assigned.</summary>
    public float NormalScale { get; init; } = 1.0f;

    /// <summary>Gets the occlusion strength, kept whether or not an occlusion texture is assigned.</summary>
    public float OcclusionStrength { get; init; } = 1.0f;

    /// <summary>
    /// Gets the alpha mode.
    /// </summary>
    public MaterialAlphaMode AlphaMode { get; init; }

    /// <summary>
    /// Gets the alpha cutoff used for masked materials.
    /// </summary>
    public float AlphaCutoff { get; init; }

    /// <summary>
    /// Gets a value indicating whether the material should be treated as double-sided.
    /// </summary>
    public bool DoubleSided { get; init; }

    /// <summary>Gets the linear emission colour, retained at zero intensity.</summary>
    public Vector3 EmissiveColor { get; init; }

    /// <summary>Gets the emission multiplier in the range [0, 65504].</summary>
    public float EmissiveIntensity { get; init; }

    // Owned immutable JSON preserves native fields outside the editable projection.
    internal JsonElement? Descriptor { get; init; }

    /// <summary>Gets a value indicating whether a channel is one of <see cref="TextureChannels"/>.</summary>
    /// <param name="channel">The channel name.</param>
    /// <returns><see langword="true"/> when the engine's schema has the channel.</returns>
    public static bool IsTextureChannel([NotNullWhen(true)] string? channel)
        => channel is not null && TextureChannels.Contains(channel, StringComparer.Ordinal);

    /// <summary>Returns a copy of this material with one texture channel assigned or cleared.</summary>
    /// <param name="channel">One of <see cref="TextureChannels"/>.</param>
    /// <param name="texture">The texture, or <see langword="null"/> to clear the channel.</param>
    /// <returns>The updated immutable material snapshot.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="channel"/> is not a texture channel, or <paramref name="texture"/> is a mount.
    /// </exception>
    public MaterialSource WithTextureReference(string channel, AssetPath? texture)
    {
        if (!IsTextureChannel(channel))
        {
            throw new ArgumentException($"'{channel}' is not a material texture channel.", nameof(channel));
        }

        if (texture?.IsMount == true)
        {
            throw new ArgumentException($"'{texture}' is a mount, not a texture below one.", nameof(texture));
        }

        var references = new Dictionary<string, AssetPath>(this.TextureReferences, StringComparer.Ordinal);
        if (texture is null)
        {
            _ = references.Remove(channel);
        }
        else
        {
            references[channel] = texture;
        }

        return this with { TextureReferences = references.ToFrozenDictionary(StringComparer.Ordinal) };
    }

    internal static MaterialAlphaMode ResolveAlphaMode(JsonElement descriptor)
    {
        var alpha = descriptor.TryGetProperty("alpha_mode", out var mode) ? mode.GetString() : "opaque";
        if (alpha is "masked" or "blended")
        {
            return string.Equals(alpha, "masked", StringComparison.Ordinal) ? MaterialAlphaMode.Mask : MaterialAlphaMode.Blend;
        }

        // Native alpha modes override surface domains; opaque leaves the domain intact.
        return descriptor.TryGetProperty("domain", out var domain) ? domain.GetString() switch
        {
            "masked" => MaterialAlphaMode.Mask,
            "alpha_blended" => MaterialAlphaMode.Blend,
            _ => MaterialAlphaMode.Opaque,
        } : MaterialAlphaMode.Opaque;
    }

    internal static bool UsesGlossiness(JsonElement parameters)
        => parameters.ValueKind == JsonValueKind.Object
            && parameters.TryGetProperty("roughness_as_glossiness", out var flag) && flag.GetBoolean();

    internal static float ResolveRoughness(JsonElement parameters)
    {
        var scalar = parameters.ValueKind == JsonValueKind.Object && parameters.TryGetProperty("roughness", out var value)
            ? value.GetSingle() : 1.0f;
        return UsesGlossiness(parameters) ? 1.0f - scalar : scalar;
    }
}
