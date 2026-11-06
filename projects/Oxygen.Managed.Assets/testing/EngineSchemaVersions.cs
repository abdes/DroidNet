// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>
/// The version of each engine format that has no managed owner, as the installed schema states it, so a test's sample of
/// the format follows the engine.
/// </summary>
public static class EngineSchemaVersions
{
    /// <summary>Gets the version of the integrity inventory.</summary>
    public static int Inventory => Of("oxygen.cooked-inventory.schema.json");

    /// <summary>Gets the version of the import report.</summary>
    public static int ImportReport => Of("oxygen.import-report.schema.json");

    /// <summary>Gets the version of the dependency report.</summary>
    public static int Dependencies => Of("oxygen.cooked-dependencies.schema.json");

    /// <summary>Gets the version of the geometry report.</summary>
    public static int Geometries => Of("oxygen.cooked-geometries.schema.json");

    /// <summary>Gets the version of the script report.</summary>
    public static int Scripts => Of("oxygen.cooked-scripts.schema.json");

    /// <summary>Gets the version of the source analysis report.</summary>
    public static int SourceAnalysis => Of("oxygen.source-analysis.schema.json");

    /// <summary>Gets the version of the asset key map.</summary>
    public static int AssetKeyMap => Of("oxygen.asset-key-map.schema.json");

    /// <summary>Gets the version of a script diagnostic.</summary>
    public static int ScriptDiagnostic => Of("oxygen.script-diagnostic.schema.json");

    /// <summary>Gets the version of surface provenance.</summary>
    public static int SurfaceProvenance => Of("oxygen.surface-provenance.schema.json");

    private static int Of(string schema)
    {
        using var stream = typeof(EngineSchemaVersions).Assembly.GetManifestResourceStream(schema)
            ?? throw new InvalidOperationException($"The test kit does not embed '{schema}'.");
        return JsonNode.Parse(stream)!["properties"]!["schema_version"]!["const"]!.GetValue<int>();
    }
}
