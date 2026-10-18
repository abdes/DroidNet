// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Schema-validated script declarations reported by the native Inspector.</summary>
public sealed class CookedScriptReport
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);
    private readonly ImmutableDictionary<Guid, ImmutableArray<CookedScriptParameter>> scripts;

    private CookedScriptReport(ImmutableDictionary<Guid, ImmutableArray<CookedScriptParameter>> scripts) => this.scripts = scripts;

    /// <summary>Validates the shared native schema and rejects duplicate scripts or parameters.</summary>
    /// <param name="json">The native report.</param>
    /// <returns>Detached typed declarations.</returns>
    public static CookedScriptReport Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!Schema.Value.Evaluate(document.RootElement).IsValid)
        {
            throw new InvalidDataException("The native script report does not match its schema.");
        }

        var scripts = ImmutableDictionary.CreateBuilder<Guid, ImmutableArray<CookedScriptParameter>>();
        foreach (var row in document.RootElement.GetProperty("scripts").EnumerateArray())
        {
            var parameters = row.GetProperty("parameters").EnumerateArray().Select(ParseParameter).ToImmutableArray();
            if (parameters.Select(static parameter => parameter.Name).Distinct(StringComparer.Ordinal).Count() != parameters.Length)
            {
                throw new InvalidDataException("The native script report declares a parameter twice.");
            }

            if (!scripts.TryAdd(row.GetProperty("asset_key").GetGuid(), parameters))
            {
                throw new InvalidDataException("The native script report contains duplicate asset keys.");
            }
        }

        return new(scripts.ToImmutable());
    }

    /// <summary>Reads a native command result scoped to one virtual path.</summary>
    /// <param name="scriptUri">The requested script identity.</param>
    /// <returns>The sole script's declarations, or null when the root has no matching script.</returns>
    public CookedScriptDeclarations? SingleOrDefault(AssetPath scriptUri)
        => this.scripts.Count switch
        {
            0 => null,
            1 => new(scriptUri, this.scripts.Keys.Single(), this.scripts.Values.Single()),
            _ => throw new InvalidDataException("The native script query returned more than one asset."),
        };

    private static CookedScriptParameter ParseParameter(JsonElement row)
    {
        var name = row.GetProperty("name").GetString()!;
        var type = row.GetProperty("type").GetString();
        var value = row.TryGetProperty("default", out var element) ? element : default;
        return type switch
        {
            "bool" => new(name, CookedScriptParameterKind.Bool, value.GetBoolean(), 0),
            "int32" => new(name, CookedScriptParameterKind.Int32, value.GetInt32(), 0),
            "float" => new(name, CookedScriptParameterKind.Float, value.GetSingle(), 0),
            "string" => new(name, CookedScriptParameterKind.String, value.GetString()!, 0),
            "vec2" => new(name, CookedScriptParameterKind.Vec2, new Vector2(value[0].GetSingle(), value[1].GetSingle()), 0),
            "vec3" => new(name, CookedScriptParameterKind.Vec3, new Vector3(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle()), 0),
            "vec4" => new(name, CookedScriptParameterKind.Vec4, new Vector4(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle(), value[3].GetSingle()), 0),
            "asset" => new(name, CookedScriptParameterKind.Asset, null, row.GetProperty("asset_type").GetByte()),
            "node" => new(name, CookedScriptParameterKind.Node, null, 0),
            _ => throw new InvalidDataException($"The native script report has an unknown parameter type '{type}'."),
        };
    }

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(CookedScriptReport).Assembly.GetManifestResourceStream("oxygen.cooked-scripts.schema.json")
            ?? throw new InvalidOperationException("The native script metadata schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }
}
