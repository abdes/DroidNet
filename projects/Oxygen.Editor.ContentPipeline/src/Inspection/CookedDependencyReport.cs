// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Immutable dependency facts read from one native cooked container.</summary>
/// <param name="SourceIdentity">The inspected container identity.</param>
/// <param name="Assets">Direct asset-key dependencies indexed by native key.</param>
/// <param name="Json">The schema-validated report retained in the derived cache.</param>
public sealed record CookedDependencyReport(Guid SourceIdentity, ImmutableDictionary<string, CookedAssetDependencies> Assets, string Json)
{
    /// <summary>The shared native report schema.</summary>
    public const string SchemaFileName = "oxygen.cooked-dependencies.schema.json";

    private static readonly Lazy<EditorSchemaCatalog> Schemas = new(static () => EditorSchemaCatalog.LoadFromDirectory(Path.Combine(AppContext.BaseDirectory, "Schemas")));

    /// <summary>Validates and detaches the native report without decoding cooked binary structures.</summary>
    /// <param name="json">The native report or verified cached report.</param>
    /// <returns>Typed container and dependency identities.</returns>
    public static CookedDependencyReport Parse(string json)
    {
        if (!Schemas.Value.ValidateAgainstEngine(SchemaFileName, JsonNode.Parse(json)))
        {
            throw new InvalidDataException("The native dependency report does not match its schema.");
        }

        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        var assets = root.GetProperty("assets").EnumerateArray().Select(static asset => new CookedAssetDependencies(
            IndexKey(asset.GetProperty("asset_key").GetString()!),
            asset.GetProperty("asset_type").GetByte(),
            asset.GetProperty("virtual_path").GetString()!,
            asset.GetProperty("dependencies").EnumerateArray().Select(static key => IndexKey(key.GetString()!)).ToImmutableArray(),
            asset.GetProperty("complete").GetBoolean(),
            asset.TryGetProperty("diagnostic", out var diagnostic) ? diagnostic.GetString() : null)).ToArray();
        var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        return assets.Any(asset => !keys.Add(asset.AssetKey))
            ? throw new InvalidDataException("The native dependency report contains duplicate asset keys.")
            : new(root.GetProperty("source_key").GetGuid(), assets.ToImmutableDictionary(static asset => asset.AssetKey, StringComparer.OrdinalIgnoreCase), json);
    }

    /// <summary>Preserves native key bytes when converting canonical text to the managed index representation.</summary>
    /// <param name="nativeKey">Canonical native key text.</param>
    /// <returns>The index key text for the same sixteen bytes.</returns>
    internal static string IndexKey(string nativeKey)
        => AssetKey.FromBytes(Guid.Parse(nativeKey).ToByteArray(bigEndian: true)).ToString();
}
