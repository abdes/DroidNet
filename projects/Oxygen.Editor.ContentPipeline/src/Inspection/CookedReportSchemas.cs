// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Json.Schema;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Loads the Inspector's report schemas, as the installed engine SDK ships them.</summary>
internal static class CookedReportSchemas
{
    private const string LodPolicySchemaFileName = "oxygen.lod-policy.schema.json";

    /// <summary>Loads a report schema that may refer to the LOD policy schema.</summary>
    /// <param name="fileName">The schema's embedded resource name.</param>
    /// <returns>The schema.</returns>
    public static JsonSchema Load(string fileName)
    {
        // The report's schemas live in their own registry: the process-wide one may already hold another build of the
        // LOD policy schema under its URI, and a second registration there is refused.
        var options = new BuildOptions { SchemaRegistry = new SchemaRegistry() };
        options.SchemaRegistry.Fetch = static (uri, _) =>
            string.Equals(Path.GetFileName(uri.LocalPath), LodPolicySchemaFileName, StringComparison.Ordinal)
                ? JsonSchema.FromText(ReadResource(LodPolicySchemaFileName), new BuildOptions { SchemaRegistry = new SchemaRegistry() })
                : null;
        return JsonSchema.FromText(ReadResource(fileName), options);
    }

    private static string ReadResource(string fileName)
    {
        using var stream = typeof(CookedReportSchemas).Assembly.GetManifestResourceStream(fileName)
            ?? throw new InvalidOperationException($"The native report schema '{fileName}' is missing.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
