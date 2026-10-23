// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Discovers a portable source bundle through native recipe analysis and captured-input proofs.</summary>
/// <param name="coordinator">The project writer and activation lifetime.</param>
/// <param name="native">The native analysis and cooking owner.</param>
public sealed class SceneImportSourceDiscovery(IContentCookCoordinator coordinator, IEngineContentPipelineApi native)
{
    /// <summary>Analyzes the actual recipe and retains every declared file and observed probe for coherent capture.</summary>
    /// <param name="operation">The operation holding the project writer.</param>
    /// <param name="sourcePath">The selected original primary source.</param>
    /// <param name="recipe">The model's destination, import policy and native source identity.</param>
    /// <param name="cancellationToken">Cancels discovery and its owned native query.</param>
    /// <param name="artifacts">Optional producer artifacts retained by the enclosing import.</param>
    /// <returns>The portable input closure, including proofs checked before retention.</returns>
    public async Task<ImportSourceBundle> DiscoverAsync(ContentCookOperation operation, string sourcePath, ContentImportJob recipe, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        coordinator.VerifyWriter(operation);
        cancellationToken.ThrowIfCancellationRequested();
        var primary = Path.GetFullPath(sourcePath);

        var directory = Path.GetDirectoryName(primary)!;
        var operationRoot = Path.Combine(operation.Project.ProjectRoot.FullPath, ".build", "cook", operation.OperationId.ToString("N"));
        var scratch = Path.Combine(operationRoot, "discovery-" + Guid.NewGuid().ToString("N"));
        CookOutputLease.RejectReparsePoint(operationRoot);
        _ = Directory.CreateDirectory(scratch);
        Task? retainedDrain = null;
        try
        {
            CookRunContext.Report(new(Message: "Reading source content and dependencies."));
            var report = await native.AnalyzeSourcesAsync(
                new(operation.OperationId, directory, scratch, [recipe with { Source = primary }])
                {
                    Artifacts = artifacts,
                }, cancellationToken).ConfigureAwait(false);
            coordinator.VerifyWriter(operation);
            var facts = report.Jobs.Single();
            if (!report.Complete || !facts.Complete)
            {
                var issues = facts.Diagnostics.Select(issue => issue with
                {
                    OperationId = operation.OperationId,
                    AffectedPath = issue.AffectedPath ?? primary,
                }).ToImmutableArray();
                if (!issues.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
                {
                    issues = issues.Add(new()
                    {
                        OperationId = operation.OperationId,
                        Domain = FailureDomain.AssetImport,
                        Severity = DiagnosticSeverity.Error,
                        Code = AssetImportDiagnosticCodes.ImportFailed,
                        Message = "Native source analysis did not complete.",
                        AffectedPath = primary,
                    });
                }

                throw new CookInputDiscoveryException(issues);
            }

            var root = CookSavedSourceReader.FindNativeInputsRoot(facts, FileSystemPath.From(directory));
            var inputs = await CookSavedSourceReader.ReadNativeInputsAsync(facts, root, ImmutableHashSet<FileSystemPath>.Empty, cancellationToken).ConfigureAwait(false);
            if (!inputs.Outside.IsEmpty)
            {
                throw new CookInputDiscoveryException(inputs.OutsideDiagnostics(operation.OperationId, FailureDomain.AssetImport, virtualPath: null));
            }

            foreach (var omitted in inputs.OmittedDiagnostics(operation.OperationId, FailureDomain.AssetImport, virtualPath: null))
            {
                CookRunContext.Report(new(omitted.Message, omitted.Severity, Diagnostic: omitted));
            }

            if (!inputs.Files.Any(file => file.Kind == CookSnapshotInputKind.File && file.SourcePath == FileSystemPath.From(primary)))
            {
                throw new InvalidDataException("Native analysis omitted its primary source file.");
            }

            return new(Path.GetRelativePath(root.FullPath, primary).Replace('\\', '/'), inputs.Files);
        }
        catch (ContentPipelineTerminationException failure)
        {
            retainedDrain = CleanupAfterDrainAsync(failure.DrainCompletion, scratch);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, retainedDrain);
        }
        finally
        {
            if (retainedDrain is null)
            {
                DeleteScratch(scratch);
            }
        }
    }

    private static async Task CleanupAfterDrainAsync(Task drain, string scratch)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            DeleteScratch(scratch);
        }
    }

    private static void DeleteScratch(string scratch)
    {
        CookOutputLease.RejectReparsePoint(scratch);
        if (Directory.Exists(scratch))
        {
            Directory.Delete(scratch, recursive: true);
        }
    }
}
