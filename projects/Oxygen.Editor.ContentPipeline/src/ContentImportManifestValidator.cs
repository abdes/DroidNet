// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Schema-aligned validator for the ED-M07 import-manifest slice.
/// </summary>
public sealed class ContentImportManifestValidator : IContentImportManifestValidator
{
    private static readonly HashSet<string> SupportedJobTypes = new(StringComparer.Ordinal)
    {
        "material-descriptor",
        "geometry-descriptor",
        "scene-descriptor",
    };

    /// <inheritdoc />
    public IReadOnlyList<DiagnosticRecord> Validate(Guid operationId, ContentImportManifest manifest)
    {
        ArgumentNullException.ThrowIfNull(manifest);

        var diagnostics = new List<DiagnosticRecord>();
        if (manifest.Version != 1)
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest version must be 1."));
        }

        if (string.IsNullOrWhiteSpace(manifest.Output))
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest output root is required."));
        }

        if (manifest.Layout is null || string.IsNullOrWhiteSpace(manifest.Layout.VirtualMountRoot))
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest virtual mount root is required."));
        }
        else if (manifest.Layout.VirtualMountRoot[0] != '/')
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest virtual mount root must start with '/'."));
        }

        if (manifest.Jobs.Count == 0)
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest requires at least one job."));
        }

        var jobIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var job in manifest.Jobs)
        {
            ValidateJob(operationId, job, jobIds, diagnostics);
        }

        foreach (var job in manifest.Jobs)
        {
            foreach (var dependency in job.DependsOn)
            {
                if (!jobIds.Contains(dependency))
                {
                    diagnostics.Add(CreateDiagnostic(
                        operationId,
                        $"Import manifest job '{job.Id}' references unknown dependency '{dependency}'."));
                }
            }
        }

        return diagnostics;
    }

    private static void ValidateJob(
        Guid operationId,
        ContentImportJob job,
        HashSet<string> jobIds,
        List<DiagnosticRecord> diagnostics)
    {
        if (string.IsNullOrWhiteSpace(job.Id))
        {
            diagnostics.Add(CreateDiagnostic(operationId, "Import manifest job id is required."));
        }
        else if (!jobIds.Add(job.Id))
        {
            diagnostics.Add(CreateDiagnostic(operationId, $"Import manifest job id '{job.Id}' is duplicated."));
        }

        if (string.IsNullOrWhiteSpace(job.Type) || !SupportedJobTypes.Contains(job.Type))
        {
            diagnostics.Add(CreateDiagnostic(operationId, $"Import manifest job '{job.Id}' has unsupported type '{job.Type}'."));
        }

        if (string.IsNullOrWhiteSpace(job.Source))
        {
            diagnostics.Add(CreateDiagnostic(operationId, $"Import manifest job '{job.Id}' source is required."));
        }

        if (job.DependsOn.Any(string.IsNullOrWhiteSpace))
        {
            diagnostics.Add(CreateDiagnostic(operationId, $"Import manifest job '{job.Id}' has an empty dependency id."));
        }
    }

    private static DiagnosticRecord CreateDiagnostic(Guid operationId, string message)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.ContentPipeline,
            Severity = DiagnosticSeverity.Error,
            Code = ContentPipelineDiagnosticCodes.ManifestGenerationFailed,
            Message = message,
        };
}
