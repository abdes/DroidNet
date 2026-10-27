// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Captures the complete saved scope before descriptor generation or native execution.</summary>
public sealed partial class ContentPipelineService
{
    private static ContentCookResult CreateFailedCook(ContentCookOperation operation, CookTargetKind targetKind, Exception exception)
        => new(
            operation.OperationId,
            targetKind,
            OperationStatus.Failed,
            [
                new DiagnosticRecord
                {
                    OperationId = operation.OperationId,
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = AssetCookDiagnosticCodes.CookFailed,
                    Message = exception.Message,
                    TechnicalMessage = exception.ToString(),
                    ExceptionType = exception.GetType().FullName,
                },
            ],
            [],
            Inspection: null,
            Validation: null);

    private static Task ReleaseArtifactsAfterDrainAsync(Task drain, NativeArtifactLease artifacts)
        => drain.ContinueWith(
            async completed =>
            {
                _ = completed.Exception;
                await artifacts.DisposeAsync().ConfigureAwait(false);
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default).Unwrap();

    private static async Task ReleaseCookOpeningAfterDrainAsync(Task drain, NativeArtifactLease artifacts, CookPublicationReadLease baseline)
    {
        using (baseline)
        {
            await ReleaseArtifactsAfterDrainAsync(drain, artifacts).ConfigureAwait(false);
        }
    }

    // An automatic cook whose sources are all gone, and at least one of them has a published product.
    private static bool IsRetiringRemovedSources(ProjectContext project, Incremental.CookProvenance previous, ContentCookInput[] primaryInputs)
        => primaryInputs.Length != 0 && primaryInputs.All(input => IsGone(project, input))
            && primaryInputs.Any(input => HasRetiredProduct(project, previous, input));

    // A cook on change of several sources leaves out those moved or deleted since they changed; the cook's repair
    // publishes the content again without their products. When every source is gone, the cook keeps them.
    private static (Func<IReadOnlyList<ContentCookScope>> ResolveScopes, ContentCookInput[] PrimaryInputs) LeaveRemovedSources(
        ContentCookOperation operation,
        Incremental.CookProvenance previous,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        ContentCookInput[] primaryInputs)
    {
        var gone = primaryInputs.Where(input => IsGone(operation.Project, input))
            .Select(static input => input.AssetUri).ToHashSet();
        if (gone.Count == 0 || primaryInputs.All(input => gone.Contains(input.AssetUri)))
        {
            return (resolveScopes, primaryInputs);
        }

        ReportRetiredSources(operation.OperationId, [.. primaryInputs.Where(input => gone.Contains(input.AssetUri) && HasRetiredProduct(operation.Project, previous, input))]);
        return (() => [.. resolveScopes().Where(scope => !scope.Inputs.All(input => gone.Contains(input.AssetUri)))], [.. primaryInputs.Where(input => !gone.Contains(input.AssetUri))]);
    }

    // A source renamed only in case is gone under its old name, although the file system still finds it by that name.
    private static bool IsGone(ProjectContext project, ContentCookInput input)
        => !ExactSourcePath.Exists(project.FileSystem, project.ProjectRoot.FullPath, input.SourceRelativePath);

    // Whether a source published a product that its move or deletion retires.
    private static bool HasRetiredProduct(ProjectContext project, Incremental.CookProvenance previous, ContentCookInput input)
        => previous.Products.Any(product => input.AssetUri == product.SourceUri && Incremental.CookRootRepair.IsRetired(project.ProjectRoot.FullPath, product));

    // Each removed source is reported as an Info diagnostic, so the run names what it removed.
    private static void ReportRetiredSources(Guid operationId, ContentCookInput[] inputs)
    {
        foreach (var input in inputs)
        {
            var retired = new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetCook,
                Severity = DiagnosticSeverity.Info,
                Code = OutputRetiredCode,
                Message = $"'{input.SourceAbsolutePath.Name}' was moved or deleted after it was cooked; this cook removes its cooked output.",
                AffectedVirtualPath = input.AssetUri.VirtualPath,
                AffectedPath = input.SourceAbsolutePath.FullPath,
            };
            CookRunContext.Report(new(retired.Message, retired.Severity, Diagnostic: retired));
        }
    }

    private static async Task<ContentCookResult?> ValidatePrimaryInputsAsync(ContentCookOperation operation, CookTargetKind targetKind, ContentCookInput[] primaryInputs, bool replacingSource, bool isAutomatic)
    {
        if (primaryInputs.Length == 0)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);
        }

        // A save or preview may have queued this cook before its source, never cooked, was moved or deleted. There is
        // nothing to cook and no output to remove.
        if (isAutomatic && primaryInputs.All(input => IsGone(operation.Project, input)))
        {
            var retired = CreateSourceMissingDiagnostics(operation.Project, operation.OperationId, primaryInputs).Select(diagnostic => diagnostic with
            {
                Severity = DiagnosticSeverity.Info,
                Message = $"'{Path.GetFileName(diagnostic.AffectedPath)}' was moved or deleted after this cook was requested; there is nothing to cook.",
            }).ToArray();
            foreach (var diagnostic in retired)
            {
                CookRunContext.Report(new(Message: diagnostic.Message));
            }

            return new(operation.OperationId, targetKind, OperationStatus.Cancelled, NormalizeDiagnostics(operation.OperationId, retired), [], Inspection: null, Validation: null);
        }

        ReportPreparing(primaryInputs);
        if (replacingSource)
        {
            return null;
        }

        var missing = CreateSourceMissingDiagnostics(operation.Project, operation.OperationId, primaryInputs);
        return missing.Count == 0 ? null : new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, missing), [], Inspection: null, Validation: null);
    }

    private static async Task<bool> InputsAreCurrentAsync(
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CancellationToken cancellationToken,
        ImmutableArray<CookProducedSourceFile> producedSourceFiles = default)
    {
        var expectedInputs = producedSourceFiles.IsDefaultOrEmpty ? snapshot.Inputs
            : CookProducedSourceFile.ExpectedInputs(snapshot.Inputs, producedSourceFiles);
        var streams = new Dictionary<FileSystemPath, FileStream>();
        try
        {
            var primary = graph.Assets.Where(static input => input.Role == ContentCookInputRole.Primary).Select(static input => input.SourceAbsolutePath).ToHashSet();
            if (!primary.SetEquals(resolveScopes().SelectMany(static scope => scope.Inputs).Select(static input => input.SourceAbsolutePath)))
            {
                return false;
            }

            foreach (var input in expectedInputs.Where(static input => input.Kind == CookSnapshotInputKind.File))
            {
                if (!streams.ContainsKey(input.SourcePath))
                {
                    streams.Add(input.SourcePath, new(input.SourcePath.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, 65536, FileOptions.Asynchronous));
                }
            }

            foreach (var input in expectedInputs)
            {
                if (input.Kind != CookSnapshotInputKind.File)
                {
                    if (!CookSavedSourceReader.MatchesProbe(input))
                    {
                        return false;
                    }
                }
                else
                {
                    var stream = streams[input.SourcePath];
                    stream.Position = 0;
                    if (!string.Equals(Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false)), input.DiscoveryHash, StringComparison.Ordinal))
                    {
                        return false;
                    }
                }
            }

            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
        finally
        {
            foreach (var stream in streams.Values)
            {
                await stream.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private async Task<ContentCookResult> CookCapturedScopesAsync(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        CancellationToken cancellationToken)
    {
        CookPublicationReadLease baseline;
        try
        {
            baseline = await this.publication.AcquireForOperationAsync(operation, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException or System.Text.Json.JsonException)
        {
            return CreateFailedCook(operation, targetKind, failure);
        }

        using var baselineLifetime = baseline;
        Incremental.CookProvenance previous;
        ContentCookInput[] primaryInputs;
        try
        {
            previous = baseline.ProductState;
            var requestedScopes = resolveScopes;
            resolveScopes = () => [.. requestedScopes().Select(Import.ImportedOutputs.ResolveScope)];
            primaryInputs = [.. resolveScopes().SelectMany(static scope => scope.Inputs)];
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
        {
            return CreateFailedCook(operation, targetKind, failure);
        }

        var scopes = resolveScopes();
        var isAutomatic = scopes.All(static scope => scope.IsAutomatic);
        if (isAutomatic)
        {
            (resolveScopes, primaryInputs) = LeaveRemovedSources(operation, previous, resolveScopes, primaryInputs);
        }

        if (isAutomatic && IsRetiringRemovedSources(operation.Project, previous, primaryInputs))
        {
            // The sources were moved or deleted after they were cooked: nothing is left to cook, and the cook's repair
            // publishes the content again without their products.
            ReportRetiredSources(operation.OperationId, primaryInputs);
            resolveScopes = static () => [];
        }
        else if (await ValidatePrimaryInputsAsync(operation, targetKind, primaryInputs, scopes.Any(static scope => scope.ImportReplacement is not null), isAutomatic).ConfigureAwait(false) is { } preflight)
        {
            return preflight;
        }

        var compatibility = await this.nativeCompatibility.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, compatibility.Diagnostics, [], Inspection: null, Validation: null);
        }

        return await this.ExecuteWithArtifactsAsync(operation, baseline, resolveScopes, targetKind, compatibility.Artifacts!, previous, cancellationToken).ConfigureAwait(false);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "the drain task takes ownership of the retained baseline and disposes it when the drain completes")]
    private async Task<ContentCookResult> ExecuteWithArtifactsAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        Incremental.CookProvenance previous,
        CancellationToken cancellationToken)
    {
        Task? retainedDrain = null;
        try
        {
            var result = await this.ExecuteIncrementalCookAsync(operation, baseline, resolveScopes, targetKind, artifacts, previous, cancellationToken).ConfigureAwait(false);
            baseline.Dispose();
            return await this.ReclaimUnusedCookedOutputAsync(operation, result).ConfigureAwait(false);
        }
        catch (CookInputDiscoveryException failure)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, failure.Diagnostics), [], Inspection: null, Validation: null);
        }
        catch (ContentPipelineTerminationException failure)
        {
            CookPublicationReadLease? retainedBaseline = null;
            try
            {
                retainedBaseline = baseline.Retain();
                retainedDrain = ReleaseCookOpeningAfterDrainAsync(failure.DrainCompletion, artifacts, retainedBaseline);
                retainedBaseline = null;
            }
            finally
            {
                retainedBaseline?.Dispose();
            }

            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, retainedDrain);
        }
        catch (NativeCompatibilityException failure)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, failure.Diagnostics), [], Inspection: null, Validation: null);
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or InvalidOperationException or System.ComponentModel.Win32Exception or System.Text.Json.JsonException)
        {
            return CreateFailedCook(operation, targetKind, failure);
        }
        finally
        {
            if (retainedDrain is null)
            {
                await artifacts.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private async Task<(CookInputSnapshot Snapshot, CookDependencyGraph Graph)> CaptureScopesAsync(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        Oxygen.Managed.Core.Compatibility.NativeArtifactLease artifacts,
        Oxygen.Editor.ContentPipeline.Incremental.CookProvenance previous,
        Publication.CookedLibraryReadSet libraries,
        CancellationToken cancellationToken)
    {
        if (resolveScopes().SingleOrDefault(static scope => scope.ImportReplacement is not null) is { } replacement)
        {
            return await this.CaptureReplacementAsync(operation, replacement, resolveScopes, artifacts, previous, libraries, cancellationToken).ConfigureAwait(false);
        }

        CookDependencyGraph? graph = null;
        var capture = new CookInputSnapshotCapture(this.cookCoordinator);
        var snapshot = await capture.CaptureAsync(
            operation,
            async token =>
            {
                var scopes = resolveScopes();
                var inputs = scopes.SelectMany(static scope => scope.Inputs).ToArray();
                graph = await this.CreateDependencyDiscovery(operation, artifacts, previous, libraries)
                    .DiscoverAsync(operation.Project, inputs, token).ConfigureAwait(false);
                graph = graph with { ImportedReferences = [.. graph.ImportedReferences.Union(scopes.SelectMany(static scope => scope.RequiredImportedOutputs))] };
                return HasError(graph.Diagnostics) ? throw new CookInputDiscoveryException(graph.Diagnostics) : graph.Files;
            },
            cancellationToken).ConfigureAwait(false);
        return (snapshot, graph!);
    }

    private CookDependencyDiscovery CreateDependencyDiscovery(
        ContentCookOperation operation,
        NativeArtifactLease artifacts,
        Incremental.CookProvenance previous,
        CookedLibraryReadSet libraries)
        => new(
            new CookSourceAnalyzer(
                operation,
                artifacts,
                this.engineContentPipelineApi,
                this.manifestBuilder,
                this.sceneDescriptorGenerator,
                this.cookScopeProvider,
                previous),
            resolveImported: uri => Import.ImportedOutputs.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency),
            resolveProducer: previous.CreateProducerLookup(operation.Project),
            preferCookedReference: libraries.IsLibraryPreferred,
            expandCookedReferences: (input, references, token) => libraries.ExpandReferencesAsync(
                input,
                references,
                this.engineContentPipelineApi as Inspection.ICookedDependencyInspector,
                Path.Combine(operation.Project.ProjectRoot.FullPath, ".build", "cook", operation.OperationId.ToString("N")),
                artifacts,
                token));
}
