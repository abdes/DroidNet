// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Reads native batch outputs, products and diagnostics from the version-three report.</summary>
internal static class NativeImportReportReader
{
    private static readonly Lazy<JsonSchema> Schema = new(static () =>
    {
        using var stream = typeof(NativeImportReportReader).Assembly.GetManifestResourceStream("Oxygen.Editor.ContentPipeline.Import.Schemas.native-import-report.schema.json")
            ?? throw new InvalidOperationException("The native import report schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    });

    /// <summary>Validates the requested jobs and detaches their outputs, products and diagnostics.</summary>
    /// <param name="json">The native report document.</param>
    /// <param name="execution">The immutable native request.</param>
    /// <param name="exitCode">The completed worker result.</param>
    /// <returns>The validated import outcome.</returns>
    public static NativeImportResult Read(string json, ContentImportExecution execution, int exitCode)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!Schema.Value.Evaluate(root).IsValid
            || FileSystemPath.From(root.GetProperty("session").GetProperty("cooked_root").GetString()!) != FileSystemPath.From(execution.Manifest.Output))
        {
            throw new InvalidDataException("The native import report does not match this operation.");
        }

        var jobs = root.GetProperty("jobs").EnumerateArray().ToArray();
        if (jobs.Length != execution.Manifest.Jobs.Count)
        {
            throw new InvalidDataException("The native import report has an incomplete job set.");
        }

        var outputsBySource = ImmutableDictionary.CreateBuilder<string, ImmutableArray<string>>(StringComparer.Ordinal);
        var productsBySource = ImmutableDictionary.CreateBuilder<string, ImmutableHashSet<string>>(StringComparer.Ordinal);
        var diagnostics = new List<DiagnosticRecord>();
        var modelImports = ImmutableDictionary.CreateBuilder<string, JsonElement>(StringComparer.Ordinal);
        var succeeded = exitCode == 0;
        var indices = new HashSet<int>();
        foreach (var job in jobs)
        {
            var index = job.GetProperty("index").GetInt32() - 1;
            if (!indices.Add(index) || index < 0 || index >= execution.Manifest.Jobs.Count
                || !string.Equals(job.GetProperty("type").GetString(), execution.Manifest.Jobs[index].ReportedType, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The native report identifies an unexpected job.");
            }

            if (execution.Importers is { } importers)
            {
                EnsureImporterRevision(job, importers);
            }

            succeeded &= string.Equals(job.GetProperty("status").GetString(), "succeeded", StringComparison.Ordinal);
            var emitted = new HashSet<string>(StringComparer.Ordinal);
            AddOutputs(job, emitted);

            var requested = execution.Manifest.Jobs[index];

            // A job without its own source, such as a script sidecar, re-emits its owner's outputs.
            if (requested.Source is { } source)
            {
                outputsBySource.Add(source, [.. emitted.Order(StringComparer.Ordinal)]);
                productsBySource.Add(source, ReadProducts(job));
            }

            ReadModelImport(job, requested, modelImports);
            AddJobDiagnostics(job, execution, requested, diagnostics);
        }

        AddIncompleteDiagnostic(execution, succeeded, diagnostics);
        return new(succeeded, diagnostics)
        {
            OutputsBySource = outputsBySource.ToImmutable(),
            ProductsBySource = productsBySource.ToImmutable(),
            ModelImports = succeeded ? modelImports.ToImmutable() : [],
        };
    }

    // A product's fingerprint holds the revision of the importer that cooks it, so a job cooked at another revision
    // would record a product the next cook wrongly reuses.
    private static void EnsureImporterRevision(JsonElement job, Incremental.ImporterRevisions importers)
    {
        if (!job.TryGetProperty("importer", out var importer))
        {
            return;
        }

        var name = importer.GetProperty("name").GetString()!;
        var revision = importer.GetProperty("revision").GetUInt32();
        if (importers.Of(name) != revision)
        {
            throw new InvalidDataException($"The ImportTool cooked with the '{name}' importer at revision {revision}, not the {importers.Of(name)} the cook's fingerprints hold.");
        }
    }

    // A successful job that imports a model establishes what its record keeps, one entry per requested source.
    private static void ReadModelImport(JsonElement job, ContentImportJob requested, ImmutableDictionary<string, JsonElement>.Builder modelImports)
    {
        if (requested.Record is null
            || !string.Equals(job.GetProperty("status").GetString(), "succeeded", StringComparison.Ordinal))
        {
            return;
        }

        if (!job.TryGetProperty("model_import", out var update))
        {
            throw new InvalidDataException("A successful model import omitted what it established.");
        }

        if (requested.Source is null || modelImports.ContainsKey(requested.Source))
        {
            throw new InvalidDataException("A model import does not identify the requested source uniquely.");
        }

        modelImports.Add(requested.Source, update.Clone());
    }

    private static void AddJobDiagnostics(JsonElement job, ContentImportExecution execution, ContentImportJob requested, List<DiagnosticRecord> diagnostics)
    {
        foreach (var issue in job.GetProperty("diagnostics").EnumerateArray())
        {
            diagnostics.Add(new()
            {
                OperationId = execution.OperationId,
                Domain = FailureDomain.AssetImport,
                Severity = issue.GetProperty("severity").GetString() switch { "info" => DiagnosticSeverity.Info, "warning" => DiagnosticSeverity.Warning, _ => DiagnosticSeverity.Error },
                Code = issue.GetProperty("code").GetString()!,
                Message = issue.GetProperty("message").GetString()!,
                AffectedPath = Path.GetFullPath(Path.Combine(execution.InputRoot, requested.AttributedSource)),
                TechnicalMessage = issue.TryGetProperty("object_path", out var location) ? location.GetString() : null,
                AffectedEntity = NativeSourceObject.Read(issue),
            });
        }
    }

    // A run that failed without reporting any error still reports that it did not complete.
    private static void AddIncompleteDiagnostic(ContentImportExecution execution, bool succeeded, List<DiagnosticRecord> diagnostics)
    {
        if (succeeded || diagnostics.Exists(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal))
        {
            return;
        }

        diagnostics.Add(new()
        {
            OperationId = execution.OperationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed,
            Message = "The native import did not complete every requested job.",
        });
    }

    private static void AddOutputs(JsonElement job, HashSet<string> outputs)
    {
        foreach (var output in job.GetProperty("outputs").EnumerateArray())
        {
            _ = outputs.Add(ReadContainedPath(output.GetProperty("path")));
        }
    }

    // A product is named by its virtual path, the identity the rest of the editor uses for it.
    private static ImmutableHashSet<string> ReadProducts(JsonElement job)
    {
        var products = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        foreach (var product in job.GetProperty("products").EnumerateArray())
        {
            _ = ReadContainedPath(product.GetProperty("descriptor_relative_path"));
            var virtualPath = product.GetProperty("virtual_path").GetString()!;
            if (!AssetPath.TryParse(virtualPath, out _) || !products.Add(virtualPath))
            {
                throw new InvalidDataException("The native report names a product that is not one asset identity.");
            }
        }

        return products.ToImmutable();
    }

    private static string ReadContainedPath(JsonElement value)
    {
        var path = value.GetString()!.Replace('\\', '/');
        return Path.IsPathRooted(path) || path.Split('/').Any(static part => part is "" or "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            ? throw new InvalidDataException("The native report contains an output outside its root.")
            : path;
    }
}
