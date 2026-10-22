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
        var provenance = ImmutableDictionary.CreateBuilder<string, NativeSurfaceProvenance>(StringComparer.Ordinal);
        var succeeded = exitCode == 0;
        var indices = new HashSet<int>();
        foreach (var job in jobs)
        {
            var index = job.GetProperty("index").GetInt32() - 1;
            if (!indices.Add(index) || index < 0 || index >= execution.Manifest.Jobs.Count
                || !string.Equals(job.GetProperty("type").GetString(), execution.Manifest.Jobs[index].Type, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The native report identifies an unexpected job.");
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

            if (requested.SurfaceProvenance is { } previous
                && string.Equals(job.GetProperty("status").GetString(), "succeeded", StringComparison.Ordinal))
            {
                if (!job.TryGetProperty("surface_provenance", out var candidate))
                {
                    throw new InvalidDataException("Successful model import omitted its surface provenance.");
                }

                var retained = NativeSurfaceProvenance.Parse(candidate);
                if (requested.Source is null || retained.SourceIdentity != previous.SourceIdentity || provenance.ContainsKey(requested.Source))
                {
                    throw new InvalidDataException("Native surface provenance does not identify the requested source uniquely.");
                }

                provenance.Add(requested.Source, retained);
            }

            foreach (var issue in job.GetProperty("diagnostics").EnumerateArray())
            {
                diagnostics.Add(new()
                {
                    OperationId = execution.OperationId,
                    Domain = FailureDomain.AssetImport,
                    Severity = issue.GetProperty("severity").GetString() switch { "info" => DiagnosticSeverity.Info, "warning" => DiagnosticSeverity.Warning, _ => DiagnosticSeverity.Error },
                    Code = issue.GetProperty("code").GetString()!,
                    Message = issue.GetProperty("message").GetString()!,
                    AffectedPath = Path.GetFullPath(Path.Combine(execution.InputRoot, execution.Manifest.Jobs[index].AttributedSource)),
                    TechnicalMessage = issue.TryGetProperty("object_path", out var location) ? location.GetString() : null,
                });
            }
        }

        if (!succeeded && !diagnostics.Exists(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal))
        {
            diagnostics.Add(new()
            {
                OperationId = execution.OperationId,
                Domain = FailureDomain.AssetImport,
                Severity = DiagnosticSeverity.Error,
                Code = AssetImportDiagnosticCodes.ImportFailed,
                Message = "The native import did not complete every requested job.",
            });
        }

        return new(succeeded, diagnostics)
        {
            OutputsBySource = outputsBySource.ToImmutable(),
            ProductsBySource = productsBySource.ToImmutable(),
            SurfaceProvenance = succeeded ? provenance.ToImmutable() : ImmutableDictionary<string, NativeSurfaceProvenance>.Empty,
        };
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
