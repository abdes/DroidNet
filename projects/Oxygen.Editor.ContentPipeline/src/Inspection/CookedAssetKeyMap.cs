// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Engine-generated identities for a known set of candidate virtual paths.</summary>
/// <param name="PathsByKey">Canonical paths indexed by the native key's managed index representation.</param>
/// <param name="Json">The schema-validated report.</param>
public sealed record CookedAssetKeyMap(ImmutableDictionary<string, string> PathsByKey, string Json)
{
    private static readonly Lazy<EditorSchemaCatalog> Schemas = new(static () => EditorSchemaCatalog.LoadFromDirectory(Path.Combine(AppContext.BaseDirectory, "Schemas")));

    /// <summary>Validates native identities and rejects ambiguous paths or keys.</summary>
    /// <param name="json">The native or integrity-checked cached report.</param>
    /// <returns>Detached native identities.</returns>
    public static CookedAssetKeyMap Parse(string json)
    {
        if (!Schemas.Value.ValidateAgainstEngine("oxygen.asset-key-map.schema.json", JsonNode.Parse(json)))
        {
            throw new InvalidDataException("The native asset key map does not match its schema.");
        }

        using var document = JsonDocument.Parse(json);
        var paths = new HashSet<string>(StringComparer.Ordinal);
        var keys = ImmutableDictionary.CreateBuilder<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in document.RootElement.GetProperty("assets").EnumerateArray())
        {
            var path = entry.GetProperty("virtual_path").GetString()!;
            if (!paths.Add(path) || !keys.TryAdd(CookedDependencyReport.IndexKey(entry.GetProperty("asset_key").GetString()!), path))
            {
                throw new InvalidDataException("The native asset key map contains duplicate paths or keys.");
            }
        }

        return new(keys.ToImmutable(), json);
    }

    /// <summary>Checks that the report covers exactly the requested candidate set.</summary>
    /// <param name="paths">Canonical paths passed to the native command.</param>
    public void ValidatePaths(IReadOnlyCollection<string> paths)
    {
        if (paths.Count != this.PathsByKey.Count || !paths.ToHashSet(StringComparer.Ordinal).SetEquals(this.PathsByKey.Values))
        {
            throw new InvalidDataException("The native asset key map does not match the requested virtual paths.");
        }
    }
}
