// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Immutable dependency facts read from one native cooked container.</summary>
/// <param name="SourceIdentity">The inspected container identity.</param>
/// <param name="Assets">Native reference facts indexed by owning asset key.</param>
/// <param name="Json">The schema-validated report retained in the derived cache.</param>
public sealed record CookedDependencyReport(Guid SourceIdentity, ImmutableDictionary<string, CookedAssetDependencies> Assets, string Json)
{
    /// <summary>The shared native report schema.</summary>
    public const string SchemaFileName = "oxygen.cooked-dependencies.schema.json";

    private static readonly Lazy<JsonSchema> Schema = new(static () =>
    {
        using var stream = typeof(CookedDependencyReport).Assembly.GetManifestResourceStream("oxygen.cooked-dependencies.schema.json")
            ?? throw new InvalidOperationException("The native cooked-dependencies schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    });

    /// <summary>Validates and detaches the native report without decoding cooked binary structures.</summary>
    /// <param name="json">The native report or verified cached report.</param>
    /// <returns>Typed container and dependency identities.</returns>
    public static CookedDependencyReport Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!Schema.Value.Evaluate(root).IsValid)
        {
            var wireSchema = root.TryGetProperty("schema", out var schema) && schema.ValueKind == JsonValueKind.String
                ? schema.GetString() : "<missing or invalid>";
            throw new InvalidDataException($"The native dependency report does not match its schema. Expected oxygen.cooked-dependencies.v2; received '{wireSchema}'. Verify that the installed native Inspector matches the current SDK source.");
        }

        var assets = root.GetProperty("assets").EnumerateArray().Select(ReadAsset).ToArray();
        var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        return assets.Any(asset => !keys.Add(asset.AssetKey))
            ? throw new InvalidDataException("The native dependency report contains duplicate asset keys.")
            : new(root.GetProperty("source_key").GetGuid(), assets.ToImmutableDictionary(static asset => asset.AssetKey, StringComparer.OrdinalIgnoreCase), json);
    }

    private static CookedAssetDependencies ReadAsset(JsonElement asset)
    {
        var keyReferences = asset.GetProperty("key_references").EnumerateArray()
            .Select(static reference => new CookedKeyReference(
                reference.GetProperty("asset_key").GetGuid().ToString("D"),
                ReadEnum<CookedKeyReferenceTargetKind>(reference.GetProperty("target_kind").GetByte(), "key reference target"),
                reference.GetProperty("expected_asset_type").GetByte()))
            .ToImmutableArray();
        var dependencies = asset.GetProperty("dependencies").EnumerateArray()
            .Select(static key => key.GetGuid().ToString("D"))
            .ToImmutableArray();
        var assetReferenceKeys = keyReferences.Where(static reference => reference.TargetKind == CookedKeyReferenceTargetKind.Asset)
            .Select(static reference => reference.AssetKey).ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (dependencies.Length != assetReferenceKeys.Count || !assetReferenceKeys.SetEquals(dependencies))
        {
            throw new InvalidDataException("The dependency report asset dependencies do not match its asset-target key references.");
        }

        var resourceBindings = asset.GetProperty("resource_bindings").EnumerateArray()
            .Select(static binding => new CookedResourceBinding(
                ReadEnum<CookedResourceKind>(binding.GetProperty("kind").GetByte(), "resource kind"),
                binding.GetProperty("index").GetUInt32(),
                ReadBindingState(binding.GetProperty("state").GetString())))
            .ToImmutableArray();
        return new CookedAssetDependencies(
            asset.GetProperty("asset_key").GetGuid().ToString("D"),
            asset.GetProperty("asset_type").GetByte(),
            asset.GetProperty("virtual_path").GetString()!,
            dependencies,
            asset.GetProperty("complete").GetBoolean(),
            asset.TryGetProperty("diagnostic", out var diagnostic) ? diagnostic.GetString() : null)
        {
            KeyReferences = keyReferences,
            ResourceBindings = resourceBindings,
        };
    }

    private static T ReadEnum<T>(byte value, string label)
        where T : struct, Enum
    {
        var parsed = (T)Enum.ToObject(typeof(T), value);
        return Enum.IsDefined(parsed) ? parsed : throw new InvalidDataException($"Unknown native {label} value {value}.");
    }

    private static CookedResourceBindingState ReadBindingState(string? state)
        => state switch
        {
            "resource" => CookedResourceBindingState.Resource,
            "error" => CookedResourceBindingState.Error,
            "fallback" => CookedResourceBindingState.Fallback,
            _ => throw new InvalidDataException($"Unknown native resource-binding state '{state}'."),
        };
}
