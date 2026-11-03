// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using Json.Schema;

namespace Oxygen.Editor.Models;

/// <summary>The engine's import manifest schema, as the installed SDK ships it: the format of import sessions and the
/// source of the ImportTool's execution defaults.</summary>
internal static class ImportManifestSchema
{
    private const string FileName = "oxygen.import-manifest.schema.json";

    private static readonly Lazy<string> Text = new(static () => EngineSchemas.ReadText(FileName));

    private static readonly Lazy<JsonSchema> Schema = new(static () => EngineSchemas.Get(FileName));

    /// <summary>Gets whether <paramref name="manifest"/> is a valid import manifest.</summary>
    /// <param name="manifest">The manifest's root.</param>
    /// <returns><see langword="true"/> when the schema accepts it.</returns>
    public static bool IsValid(JsonElement manifest) => Schema.Value.Evaluate(manifest).IsValid;

    /// <summary>Gets the schema's root object, to read the defaults it states.</summary>
    /// <returns>A fresh copy of the schema.</returns>
    public static JsonObject Read() => JsonNode.Parse(Text.Value)!.AsObject();
}
