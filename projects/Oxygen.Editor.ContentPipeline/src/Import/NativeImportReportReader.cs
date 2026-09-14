// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Reads native batch output ownership and diagnostics from the existing version-two report.</summary>
internal static class NativeImportReportReader
{
    private static readonly Lazy<JsonSchema> Schema = new(static () =>
    {
        using var stream = typeof(NativeImportReportReader).Assembly.GetManifestResourceStream("Oxygen.Editor.ContentPipeline.Import.Schemas.native-import-report.schema.json")
            ?? throw new InvalidOperationException("The native import report schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    });

    /// <summary>Validates the requested jobs and detaches their output ownership and diagnostics.</summary>
    /// <param name="json">The native report document.</param>
    /// <param name="execution">The immutable native request.</param>
    /// <param name="exitCode">The completed worker result.</param>
    /// <returns>The validated import outcome.</returns>
    public static NativeImportResult Read(string json, ContentImportExecution execution, int exitCode)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!Schema.Value.Evaluate(root).IsValid
            || !string.Equals(Path.GetFullPath(root.GetProperty("session").GetProperty("cooked_root").GetString()!), Path.GetFullPath(execution.Manifest.Output), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The native import report does not match this operation.");
        }

        var jobs = root.GetProperty("jobs").EnumerateArray().ToArray();
        if (jobs.Length != execution.Manifest.Jobs.Count)
        {
            throw new InvalidDataException("The native import report has an incomplete job set.");
        }

        var outputs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var diagnostics = new List<DiagnosticRecord>();
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
            AddOutputs(job, outputs);

            foreach (var issue in job.GetProperty("diagnostics").EnumerateArray())
            {
                diagnostics.Add(new()
                {
                    OperationId = execution.OperationId, Domain = FailureDomain.AssetImport,
                    Severity = issue.GetProperty("severity").GetString() switch { "info" => DiagnosticSeverity.Info, "warning" => DiagnosticSeverity.Warning, _ => DiagnosticSeverity.Error },
                    Code = issue.GetProperty("code").GetString()!, Message = issue.GetProperty("message").GetString()!,
                    AffectedPath = Path.GetFullPath(Path.Combine(execution.InputRoot, execution.Manifest.Jobs[index].Source)),
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

        return new(succeeded, diagnostics) { OutputFiles = outputs.Order(StringComparer.Ordinal).ToArray() };
    }

    private static void AddOutputs(JsonElement job, HashSet<string> outputs)
    {
        foreach (var output in job.GetProperty("outputs").EnumerateArray())
        {
            var path = output.GetProperty("path").GetString()!.Replace('\\', '/');
            if (Path.IsPathRooted(path) || path.Split('/').Any(static part => part is "" or "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0))
            {
                throw new InvalidDataException("The native report contains an output outside its root.");
            }

            _ = outputs.Add(path);
        }
    }
}
