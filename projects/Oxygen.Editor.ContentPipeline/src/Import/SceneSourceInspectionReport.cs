// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Source facts reported by the native importer without generating cooked content.</summary>
/// <param name="Parsed">Whether source metadata could be read.</param>
/// <param name="Supported">Whether all source features meet the static/scalar import policy.</param>
/// <param name="Format">The native format, gltf or fbx.</param>
/// <param name="ExternalFiles">Decoded source-relative file paths, excluding embedded data.</param>
/// <param name="Coordinates">Native source and conversion facts.</param>
/// <param name="MeshCount">Number of source meshes.</param>
/// <param name="MaterialCount">Number of source materials.</param>
/// <param name="NodeCount">Number of source nodes.</param>
/// <param name="Diagnostics">Native issues correlated to the owning operation.</param>
public sealed record SceneSourceInspectionReport(
    bool Parsed,
    bool Supported,
    string Format,
    ImmutableArray<string> ExternalFiles,
    SourceCoordinateSystem Coordinates,
    long MeshCount,
    long MaterialCount,
    long NodeCount,
    ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>The native schema shared by the cooker and editor.</summary>
    public const string SchemaFileName = "oxygen.scene-source-inspection.schema.json";

    /// <summary>Validates and detaches the versioned native report.</summary>
    /// <param name="json">The native report.</param>
    /// <param name="operationId">The owning operation's correlation identity.</param>
    /// <param name="schemas">The matched native schemas.</param>
    /// <returns>Immutable source facts and diagnostics.</returns>
    public static SceneSourceInspectionReport Parse(string json, Guid operationId, EditorSchemaCatalog schemas)
    {
        ArgumentNullException.ThrowIfNull(schemas);
        if (!schemas.ValidateAgainstEngine(SchemaFileName, JsonNode.Parse(json)))
        {
            throw new InvalidDataException("The engine returned an invalid source-inspection report.");
        }

        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        var axes = root.GetProperty("source_axes");
        var units = root.GetProperty("source_unit_meters");
        var handedness = root.GetProperty("source_left_handed");
        var diagnostics = root.GetProperty("diagnostics").EnumerateArray().Select(item => new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = item.GetProperty("severity").GetString() switch
            {
                "Info" => DiagnosticSeverity.Info,
                "Warning" => DiagnosticSeverity.Warning,
                _ => DiagnosticSeverity.Error,
            },
            Code = item.GetProperty("code").GetString()!,
            Message = item.GetProperty("message").GetString()!,
            AffectedPath = item.GetProperty("source_path").GetString(),
            TechnicalMessage = item.GetProperty("object_path").GetString(),
        }).ToImmutableArray();

        return new(
            root.GetProperty("parsed").GetBoolean(),
            root.GetProperty("supported").GetBoolean(),
            root.GetProperty("format").GetString()!,
            root.GetProperty("external_files").EnumerateArray().Select(static item => item.GetString()!).ToImmutableArray(),
            new(
                units.ValueKind == JsonValueKind.Null ? null : units.GetDouble(),
                axes.GetProperty("right").GetString()!,
                axes.GetProperty("up").GetString()!,
                axes.GetProperty("front").GetString()!,
                handedness.ValueKind == JsonValueKind.Null ? null : handedness.GetBoolean(),
                root.GetProperty("reverses_winding").GetBoolean()),
            root.GetProperty("mesh_count").GetInt64(),
            root.GetProperty("material_count").GetInt64(),
            root.GetProperty("node_count").GetInt64(),
            diagnostics);
    }
}
