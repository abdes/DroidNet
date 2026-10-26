// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;

namespace Oxygen.Editor.Models.TestSupport;

/// <summary>The installed engine schemas, copied beside the tests, which the module's vocabulary must match.</summary>
internal static class InstalledSchemas
{
    /// <summary>Reads the import manifest schema.</summary>
    /// <returns>The schema's root.</returns>
    public static JsonObject ImportManifest() => Read("oxygen.import-manifest.schema.json");

    /// <summary>Reads the source analysis schema.</summary>
    /// <returns>The schema's root.</returns>
    public static JsonObject SourceAnalysis() => Read("oxygen.source-analysis.schema.json");

    /// <summary>Gets a job key's schema in the import manifest, its <c>$ref</c> resolved.</summary>
    /// <param name="manifest">The import manifest schema.</param>
    /// <param name="key">A job key.</param>
    /// <returns>The key's schema.</returns>
    public static JsonObject JobKey(JsonObject manifest, string key)
    {
        var definitions = manifest["definitions"]!.AsObject();
        var schema = definitions["job_settings"]!["properties"]![key]!.AsObject();
        return schema["$ref"] is { } reference
            ? definitions[reference.GetValue<string>()["#/definitions/".Length..]]!.AsObject()
            : schema;
    }

    private static JsonObject Read(string name)
        => JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Schemas", name)))!.AsObject();
}
