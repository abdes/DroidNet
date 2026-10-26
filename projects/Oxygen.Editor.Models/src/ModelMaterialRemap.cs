// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Oxygen.Editor.Models;

/// <summary>Reads and writes a model's material remap: a source material, by the name the import gives it, uses a
/// project material, by its virtual path, and the import does not produce it.</summary>
public static class ModelMaterialRemap
{
    /// <summary>Gets the remap the settings hold.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <returns>The project material of each remapped source material; empty when the settings remap none.</returns>
    public static ImmutableDictionary<string, string> Read(JsonElement settings)
        => settings.ValueKind == JsonValueKind.Object
            && settings.TryGetProperty(ModelImportSettingCatalog.MaterialRemapKey, out var remap)
            && remap.ValueKind == JsonValueKind.Object
                ? remap.EnumerateObject().Where(static entry => entry.Value.ValueKind == JsonValueKind.String)
                    .ToImmutableDictionary(static entry => entry.Name, static entry => entry.Value.GetString()!, StringComparer.Ordinal)
                : ImmutableDictionary<string, string>.Empty.WithComparers(StringComparer.Ordinal);

    /// <summary>Gets the settings with <paramref name="material"/> remapped to <paramref name="target"/>, or imported
    /// again for <see langword="null"/>. An empty remap leaves the settings without the key.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="material">The source material's name in the import.</param>
    /// <param name="target">The project material's virtual path, or <see langword="null"/> to import the material.</param>
    /// <returns>The changed settings.</returns>
    public static JsonElement WriteTo(JsonElement settings, string material, string? target)
    {
        ArgumentException.ThrowIfNullOrEmpty(material);
        var remap = Read(settings);
        remap = target is null ? remap.Remove(material) : remap.SetItem(material, target);
        var copy = settings.ValueKind == JsonValueKind.Object ? JsonNode.Parse(settings.GetRawText())!.AsObject() : [];
        if (remap.IsEmpty)
        {
            _ = copy.Remove(ModelImportSettingCatalog.MaterialRemapKey);
        }
        else
        {
            var entries = new JsonObject();
            foreach (var (name, path) in remap.OrderBy(static entry => entry.Key, StringComparer.Ordinal))
            {
                entries[name] = path;
            }

            copy[ModelImportSettingCatalog.MaterialRemapKey] = entries;
        }

        return JsonSerializer.SerializeToElement(copy);
    }
}
