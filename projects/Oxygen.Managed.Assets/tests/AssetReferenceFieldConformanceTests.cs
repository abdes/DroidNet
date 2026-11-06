// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>
/// The engine's schemas are the authority on which fields hold a reference: each uses the shared <c>reference</c>
/// definition. A field added to a schema without the type table fails here.
/// </summary>
[TestClass]
public sealed class AssetReferenceFieldConformanceTests
{
    private const string ReferenceSchema = "oxygen.reference.schema.json";

    [TestMethod]
    [DataRow(AssetFileTypes.MaterialDescriptor, "oxygen.material-descriptor.schema.json")]
    [DataRow(AssetFileTypes.GeometryDescriptor, "oxygen.geometry-descriptor.schema.json")]
    [DataRow(AssetFileTypes.CollisionShapeDescriptor, "oxygen.collision-shape-descriptor.schema.json")]
    [DataRow(AssetFileTypes.PhysicsMaterialDescriptor, "oxygen.physics-material-descriptor.schema.json")]
    [DataRow(AssetFileTypes.PhysicsSceneDescriptor, "oxygen.physics-sidecar.schema.json")]
    [DataRow(AssetFileTypes.TextureDescriptor, "oxygen.texture-descriptor.schema.json")]
    [DataRow(AssetFileTypes.InputFile, "oxygen.input.schema.json")]
    [DataRow(AssetFileTypes.BufferContainer, "oxygen.buffer-container.schema.json")]
    [DataRow(AssetFileTypes.ImportSettings, "oxygen.model-import.schema.json")]
    public void TheTypeTableNamesTheFieldsTheSchemaDoes(string suffix, string schemaFile)
    {
        var schema = JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Schemas", schemaFile)))!.AsObject();
        var declared = new HashSet<string>(StringComparer.Ordinal);
        Walk(schema, [], schema["definitions"] as JsonObject, [], declared);
        var fields = AssetFileTypes.Find("Name" + suffix)!.ReferenceFields.Select(static field => field.Path).ToArray();

        _ = declared.Where(path => !fields.Any(field => Matches(field, path))).Should().BeEmpty("every reference of the schema is in the type table");
        _ = fields.Where(field => !declared.Any(path => Matches(field, path))).Should().BeEmpty("every field of the type table is a reference of the schema");
    }

    // A table field's '*' stands for any one segment of the schema's path, a named one or an array's or a map's '*'.
    private static bool Matches(string field, string path)
    {
        var expected = field.Split('/');
        var actual = path.Split('/');
        return expected.Length == actual.Length && expected.Zip(actual).All(static pair => pair.First == "*" || pair.First == pair.Second);
    }

    private static void Walk(JsonNode? node, string[] path, JsonObject? definitions, HashSet<(string Name, string Path)> visited, HashSet<string> found)
    {
        if (node is not JsonObject schema)
        {
            return;
        }

        var target = schema["$ref"]?.GetValue<string>();
        if (target?.StartsWith(ReferenceSchema, StringComparison.Ordinal) == true)
        {
            _ = found.Add(string.Join('/', path));
        }
        else if (target?.StartsWith("#/definitions/", StringComparison.Ordinal) == true)
        {
            var name = target["#/definitions/".Length..];
            if (visited.Add((name, string.Join('/', path))))
            {
                Walk(definitions?[name], path, definitions, visited, found);
            }
        }

        foreach (var keyword in (string[])["allOf", "anyOf", "oneOf"])
        {
            foreach (var branch in schema[keyword] as JsonArray ?? [])
            {
                Walk(branch, path, definitions, visited, found);
            }
        }

        foreach (var (name, value) in schema["properties"] as JsonObject ?? [])
        {
            Walk(value, [.. path, name], definitions, visited, found);
        }

        Walk(schema["items"], [.. path, "*"], definitions, visited, found);
        Walk(schema["additionalProperties"], [.. path, "*"], definitions, visited, found);
        foreach (var (_, value) in schema["patternProperties"] as JsonObject ?? [])
        {
            Walk(value, [.. path, "*"], definitions, visited, found);
        }
    }
}
