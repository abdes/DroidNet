// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;

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
    /// Initializes a new instance of the <see cref="MaterialSource"/> class.
    /// </summary>
    /// <param name="name">An optional debugging/display name.</param>
    /// <param name="pbrMetallicRoughness">The PBR metallic-roughness parameters.</param>
    /// <param name="normalTexture">The optional normal texture reference and scale.</param>
    /// <param name="occlusionTexture">The optional occlusion texture reference and strength.</param>
    /// <param name="alphaMode">The alpha mode semantics.</param>
    /// <param name="alphaCutoff">The alpha cutoff used for <see cref="MaterialAlphaMode.Mask"/>.</param>
    /// <param name="doubleSided">Whether the material should be treated as double-sided.</param>
    /// <param name="emissiveColor">Linear emission colour; defaults to white.</param>
    /// <param name="emissiveIntensity">Finite HDR emission multiplier.</param>
    public MaterialSource(
        string? name,
        MaterialPbrMetallicRoughness pbrMetallicRoughness,
        NormalTextureRef? normalTexture,
        OcclusionTextureRef? occlusionTexture,
        MaterialAlphaMode alphaMode,
        float alphaCutoff,
        bool doubleSided,
        Vector3? emissiveColor = null,
        float emissiveIntensity = 0.0f)
    {
        ArgumentNullException.ThrowIfNull(pbrMetallicRoughness);
        this.Name = name;
        this.PbrMetallicRoughness = pbrMetallicRoughness;
        this.NormalTexture = normalTexture;
        this.OcclusionTexture = occlusionTexture;
        this.NormalScale = normalTexture?.Scale ?? 1.0f;
        this.OcclusionStrength = occlusionTexture?.Strength ?? 1.0f;
        this.AlphaMode = alphaMode;
        this.AlphaCutoff = alphaCutoff;
        this.DoubleSided = doubleSided;
        this.EmissiveColor = emissiveColor ?? Vector3.One;
        this.EmissiveIntensity = emissiveIntensity;
        this.TextureReferences = CreateTextureReferences(pbrMetallicRoughness, normalTexture, occlusionTexture);
    }

    /// <summary>
    /// Gets the optional debugging/display name.
    /// </summary>
    public string? Name { get; init; }

    /// <summary>
    /// Gets the PBR metallic-roughness parameters.
    /// </summary>
    public MaterialPbrMetallicRoughness PbrMetallicRoughness { get; init; }

    /// <summary>
    /// Gets the optional normal texture reference and scale.
    /// </summary>
    public NormalTextureRef? NormalTexture { get; init; }

    /// <summary>
    /// Gets the optional occlusion texture reference and strength.
    /// </summary>
    public OcclusionTextureRef? OcclusionTexture { get; init; }

    /// <summary>Gets or sets the per-channel canonical native virtual texture paths.</summary>
    public IReadOnlyDictionary<string, string> TextureReferences { get; init; }

    /// <summary>Gets or sets the normal-map scale, retained independently of its optional binding.</summary>
    public float NormalScale { get; init; }

    /// <summary>Gets or sets the occlusion strength, retained independently of its optional binding.</summary>
    public float OcclusionStrength { get; init; }

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

    /// <summary>Enumerates every native texture dependency, including bindings outside the scalar editor.</summary>
    /// <returns>Canonical absolute virtual paths; repeated bindings may name the same asset.</returns>
    public IEnumerable<string> EnumerateTextureVirtualPaths()
    {
        foreach (var path in this.TextureReferences.Values)
        {
            yield return path;
        }
    }

    /// <summary>Returns a copy of this material with one texture channel assigned or cleared.</summary>
    /// <param name="channel">A channel supported by the native material descriptor schema.</param>
    /// <param name="virtualPath">The canonical absolute texture virtual path, or null to clear.</param>
    /// <returns>The updated immutable material snapshot.</returns>
    public MaterialSource WithTextureReference(string channel, string? virtualPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channel);
        if (!TextureChannels.Contains(channel, StringComparer.Ordinal))
        {
            throw new ArgumentException($"Unsupported material texture channel '{channel}'.", nameof(channel));
        }

        var references = this.TextureReferences.ToDictionary(static pair => pair.Key, static pair => pair.Value, StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(virtualPath))
        {
            _ = references.Remove(channel);
            virtualPath = null;
        }
        else
        {
            references[channel] = virtualPath;
        }

        var pbr = this.PbrMetallicRoughness;
        var baseColor = string.Equals(channel, "base_color"
, StringComparison.Ordinal) ? virtualPath is null ? null : new(virtualPath)
            : pbr.BaseColorTexture;
        return this with
        {
            TextureReferences = references,
            PbrMetallicRoughness = new MaterialPbrMetallicRoughness(
                pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA,
                pbr.MetallicFactor, pbr.RoughnessFactor, baseColor, pbr.MetallicRoughnessTexture),
            NormalTexture = string.Equals(channel, "normal"
, StringComparison.Ordinal) ? virtualPath is null ? null : new(virtualPath, this.NormalScale)
                : this.NormalTexture,
            OcclusionTexture = string.Equals(channel, "ambient_occlusion"
, StringComparison.Ordinal) ? virtualPath is null ? null : new(virtualPath, this.OcclusionStrength)
                : this.OcclusionTexture,
        };
    }

    /// <summary>Lists all editable texture binding names from the current native schema.</summary>
    public static IReadOnlyList<string> TextureChannels { get; } =
    [
        "base_color", "normal", "metallic", "roughness", "ambient_occlusion", "emissive",
        "specular", "sheen_color", "clearcoat", "clearcoat_normal", "transmission", "thickness",
    ];

    private static IReadOnlyDictionary<string, string> CreateTextureReferences(
        MaterialPbrMetallicRoughness pbr,
        NormalTextureRef? normal,
        OcclusionTextureRef? occlusion)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        Add("base_color", pbr.BaseColorTexture?.Source);
        Add("metallic", pbr.MetallicRoughnessTexture?.Source);
        Add("roughness", pbr.MetallicRoughnessTexture?.Source);
        Add("normal", normal?.Source);
        Add("ambient_occlusion", occlusion?.Source);
        return result;

        void Add(string channel, string? path)
        {
            if (path is not null)
            {
                result[channel] = path;
            }
        }
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

    internal static string? PackedMetallicRoughnessPath(JsonElement textures)
    {
        if (textures.ValueKind != JsonValueKind.Object
            || !textures.TryGetProperty("metallic", out var metallic)
            || !textures.TryGetProperty("roughness", out var roughness))
        {
            return null;
        }

        return JsonNode.DeepEquals(JsonNode.Parse(metallic.GetRawText()), JsonNode.Parse(roughness.GetRawText()))
            ? metallic.GetProperty("virtual_path").GetString() : null;
    }
}
