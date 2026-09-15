// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Preserves native descriptor payloads without projecting or duplicating their defaults.</summary>
public sealed partial class BuiltinGeometryCatalog
{
    private readonly JsonElement source;

    private BuiltinGeometryCatalog(string mountName, BuiltinDescriptorContribution defaultMaterial, ImmutableArray<BuiltinGeometryDefinition> geometries, JsonElement source)
    {
        this.MountName = mountName;
        this.DefaultMaterial = defaultMaterial;
        this.Geometries = geometries;
        this.source = source;
    }

    /// <summary>Gets the native output mount selected for this catalog.</summary>
    public string MountName { get; }

    /// <summary>Gets the engine default material's cook contribution.</summary>
    public BuiltinDescriptorContribution DefaultMaterial { get; }

    /// <summary>Gets all native definitions, including internal tool resources.</summary>
    public ImmutableArray<BuiltinGeometryDefinition> Geometries { get; }

    /// <summary>Gets the engine's standard and advanced authoring choices.</summary>
    public IEnumerable<BuiltinGeometryDefinition> AuthoringGeometries
        => this.Geometries.Where(static definition => definition.AuthoringCategory != GeneratedAssetCategory.Internal);

    /// <summary>Reads the versioned native catalog and takes ownership of its immutable JSON payloads.</summary>
    /// <param name="json">The native catalog document.</param>
    /// <returns>The detached catalog.</returns>
    public static BuiltinGeometryCatalog Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!string.Equals(root.GetProperty("schema").GetString(), "oxygen.builtin-geometry-catalog.v2", StringComparison.Ordinal))
        {
            throw new InvalidDataException("The engine builtin geometry catalog version is not supported.");
        }

        var mount = RequiredString(root, "mount");
        var material = ReadContribution(root.GetProperty("default_material"));
        var geometries = root.GetProperty("geometries").EnumerateArray().Select(static item => new BuiltinGeometryDefinition(
            new Uri(RequiredString(item, "asset_uri"), UriKind.Absolute),
            RequiredString(item, "name"),
            RequiredString(item, "canonical_name"),
            ReadContribution(item),
            ReadAuthoringCategory(item))).ToImmutableArray();
        var identities = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        return geometries.Any(item => !identities.Add(item.AssetUri.AbsoluteUri))
            ? throw new InvalidDataException("The engine builtin catalog contains duplicate authored identities.")
            : new(mount, material, geometries, root.Clone());
    }

    /// <summary>Preserves the complete native document for the derived discovery cache.</summary>
    /// <returns>The native catalog JSON, including internal resources and descriptor payloads.</returns>
    public string ToJson() => this.source.GetRawText();

    /// <summary>Finds a native definition while preserving the caller's authored URI.</summary>
    /// <param name="assetUri">The requested built-in URI.</param>
    /// <returns>The matching authorable definition, or null for an unsupported or internal resource.</returns>
    public BuiltinGeometryDefinition? Find(Uri assetUri)
        => this.AuthoringGeometries.FirstOrDefault(item => string.Equals(item.AssetUri.AbsoluteUri, assetUri.AbsoluteUri, StringComparison.OrdinalIgnoreCase));

    private static GeneratedAssetCategory ReadAuthoringCategory(JsonElement item)
        => RequiredString(item, "authoring_category") switch
        {
            "standard" => GeneratedAssetCategory.Standard,
            "advanced" => GeneratedAssetCategory.Advanced,
            "internal" => GeneratedAssetCategory.Internal,
            _ => throw new InvalidDataException("The engine builtin geometry authoring category is not supported."),
        };

    private static string RequiredString(JsonElement element, string name)
        => element.TryGetProperty(name, out var property) && property.ValueKind == JsonValueKind.String && property.GetString() is { Length: > 0 } value
            ? value : throw new InvalidDataException($"The engine builtin catalog is missing '{name}'.");

    private static BuiltinDescriptorContribution ReadContribution(JsonElement item)
    {
        var descriptor = item.GetProperty("descriptor");
        var name = RequiredString(descriptor, "name");
        return name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            ? throw new InvalidDataException("The engine builtin descriptor name is not a valid file name.")
            : new(name, RequiredString(item, "virtual_path"), descriptor.Clone());
    }
}
