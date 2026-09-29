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
        this.AlphaMode = alphaMode;
        this.AlphaCutoff = alphaCutoff;
        this.DoubleSided = doubleSided;
        this.EmissiveColor = emissiveColor ?? Vector3.One;
        this.EmissiveIntensity = emissiveIntensity;
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
        if (this.Descriptor is { } descriptor)
        {
            if (descriptor.TryGetProperty("textures", out var textures))
            {
                foreach (var binding in textures.EnumerateObject())
                {
                    yield return binding.Value.GetProperty("virtual_path").GetString()!;
                }
            }

            yield break;
        }

        foreach (var path in new[]
        {
            this.PbrMetallicRoughness.BaseColorTexture?.Source,
            this.PbrMetallicRoughness.MetallicRoughnessTexture?.Source,
            this.NormalTexture?.Source,
            this.OcclusionTexture?.Source,
        })
        {
            if (path is not null)
            {
                yield return path;
            }
        }
    }

    internal static MaterialAlphaMode ResolveAlphaMode(JsonElement descriptor)
    {
        var alpha = descriptor.TryGetProperty("alpha_mode", out var mode) ? mode.GetString() : "opaque";
        if (alpha is "masked" or "blended")
        {
            return alpha == "masked" ? MaterialAlphaMode.Mask : MaterialAlphaMode.Blend;
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
