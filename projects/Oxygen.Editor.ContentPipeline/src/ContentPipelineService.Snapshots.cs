// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
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
        Import.ImportedSourceIndex imports;
        ContentCookInput[] primaryInputs;
        try
        {
            (previous, imports, resolveScopes) = await this.ResolveImportOwnershipAsync(operation, baseline, resolveScopes, cancellationToken).ConfigureAwait(false);
            primaryInputs = resolveScopes().SelectMany(static scope => scope.Inputs).ToArray();
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
        {
            return CreateFailedCook(operation, targetKind, failure);
        }

        var scopes = resolveScopes();
        if (await this.ValidatePrimaryInputsAsync(operation, targetKind, primaryInputs, scopes.Any(static scope => scope.ImportReplacement is not null), scopes.All(static scope => scope.IsAutomatic), cancellationToken).ConfigureAwait(false) is { } preflight)
        {
            return preflight;
        }

        var compatibility = await this.nativeCompatibility.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, compatibility.Diagnostics, [], Inspection: null, Validation: null);
        }

        return await this.ExecuteWithArtifactsAsync(operation, baseline, resolveScopes, targetKind, compatibility.Artifacts!, previous, imports, cancellationToken).ConfigureAwait(false);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "the drain task takes ownership of the retained baseline and disposes it when the drain completes")]
    private async Task<ContentCookResult> ExecuteWithArtifactsAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        Incremental.CookProvenance previous,
        Import.ImportedSourceIndex imports,
        CancellationToken cancellationToken)
    {
        Task? retainedDrain = null;
        try
        {
            var result = await this.ExecuteIncrementalCookAsync(operation, baseline, resolveScopes, targetKind, artifacts, previous, imports, cancellationToken).ConfigureAwait(false);
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

    private async Task<ContentCookResult?> ValidatePrimaryInputsAsync(ContentCookOperation operation, CookTargetKind targetKind, ContentCookInput[] primaryInputs, bool replacingSource, bool isAutomatic, CancellationToken cancellationToken)
    {
        if (primaryInputs.Length == 0)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);
        }

        // A save or preview may have queued this cook before the Content Browser moved or deleted its source.
        // There is nothing left to cook; the next cook retires the old output.
        if (isAutomatic && primaryInputs.All(static input => !File.Exists(input.SourceAbsolutePath)))
        {
            var retired = CreateSourceMissingDiagnostics(operation.OperationId, primaryInputs).Select(diagnostic => diagnostic with
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

        await this.RequireSavedDocumentsAsync(primaryInputs, cancellationToken).ConfigureAwait(false);
        if (replacingSource)
        {
            return null;
        }

        var missing = CreateSourceMissingDiagnostics(operation.OperationId, primaryInputs);
        return missing.Count == 0 ? null : new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, missing), [], Inspection: null, Validation: null);
    }

    private async Task<(Incremental.CookProvenance previous, Import.ImportedSourceIndex imports, Func<IReadOnlyList<ContentCookScope>> scopes)> ResolveImportOwnershipAsync(
        ContentCookOperation operation, CookPublicationReadLease baseline, Func<IReadOnlyList<ContentCookScope>> scopes, CancellationToken cancellationToken)
    {
        var previous = baseline.ProductState;
        var imports = await Import.ImportedSourceIndex.ReadAsync(operation.Project, cookDocuments, previous, cancellationToken).ConfigureAwait(false);
        return (previous, imports, () => scopes().Select(imports.ResolveScope).ToArray());
    }

    private async Task<(CookInputSnapshot snapshot, CookDependencyGraph graph)> CaptureScopesAsync(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        Oxygen.Managed.Core.Compatibility.NativeArtifactLease artifacts,
        Oxygen.Editor.ContentPipeline.Incremental.CookProvenance previous,
        Import.ImportedSourceIndex imports,
        Publication.CookedLibraryReadSet libraries,
        CancellationToken cancellationToken)
    {
        if (resolveScopes().SingleOrDefault(static scope => scope.ImportReplacement is not null) is { } replacement)
        {
            return await this.CaptureReplacementAsync(operation, replacement, resolveScopes, artifacts, previous, imports, libraries, cancellationToken).ConfigureAwait(false);
        }

        CookDependencyGraph? graph = null;
        var capture = new CookInputSnapshotCapture(cookDocuments, this.cookCoordinator);
        var captured = await capture.CaptureAsync(
            operation,
            async token =>
            {
                var scopes = resolveScopes();
                var inputs = scopes.SelectMany(static scope => scope.Inputs).ToArray();
                graph = await this.CreateDependencyDiscovery(operation, artifacts, previous, imports, libraries)
                    .DiscoverAsync(operation.Project, inputs, token).ConfigureAwait(false);
                graph = graph with { ImportedReferences = [.. graph.ImportedReferences.Union(scopes.SelectMany(static scope => scope.RequiredImportedOutputs))] };
                return HasError(graph.Diagnostics) ? throw new CookInputDiscoveryException(graph.Diagnostics) : graph.Files;
            },
            artifacts.Fingerprint,
            cancellationToken).ConfigureAwait(false);
        return captured switch
        {
            { NeedsSave.IsEmpty: false } => throw new CookInputsNeedSaveException(captured.NeedsSave),
            { ExternalChanges.IsEmpty: false } => throw new IOException("Reload changed source before cooking: " + string.Join(", ", captured.ExternalChanges.Select(static document => document.DisplayName))),
            { Snapshot: { } snapshot } => (snapshot, graph!),
            _ => throw new InvalidOperationException("Input capture did not produce a snapshot."),
        };
    }

    private CookDependencyDiscovery CreateDependencyDiscovery(
        ContentCookOperation operation,
        NativeArtifactLease artifacts,
        Incremental.CookProvenance previous,
        Import.ImportedSourceIndex imports,
        CookedLibraryReadSet libraries)
        => new(
            new CookSourceAnalyzer(
                operation,
                artifacts,
                this.engineContentPipelineApi,
                this.manifestBuilder,
                this.sceneDescriptorGenerator,
                this.cookScopeProvider,
                cookDocuments,
                previous),
            resolveImported: uri => imports.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency),
            resolveBufferOwner: path => previous.FindBufferOwner(operation.Project, path),
            preferCookedReference: libraries.IsLibraryPreferred,
            expandCookedReferences: (input, references, token) => libraries.ExpandReferencesAsync(
                input,
                references,
                this.engineContentPipelineApi as Inspection.ICookedDependencyInspector,
                Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")),
                artifacts,
                token));

    private async Task<bool> InputsAreCurrentAsync(
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CancellationToken cancellationToken,
        ImmutableArray<CookProducedSourceFile> producedSourceFiles = default)
    {
        var expectedInputs = producedSourceFiles.IsDefaultOrEmpty ? snapshot.Inputs
            : CookProducedSourceFile.ExpectedInputs(snapshot.Inputs, producedSourceFiles);
        using var reads = await cookDocuments.AcquireAsync(expectedInputs.Select(static input => input.SourcePath), cancellationToken).ConfigureAwait(false);
        if (reads.Documents.Any(static document => document.IsDirty))
        {
            return false;
        }

        var streams = new Dictionary<string, FileStream>(StringComparer.OrdinalIgnoreCase);
        try
        {
            var primary = graph.Assets.Where(static input => input.Role == ContentCookInputRole.Primary).Select(static input => input.SourceAbsolutePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
            if (!primary.SetEquals(resolveScopes().SelectMany(static scope => scope.Inputs).Select(static input => input.SourceAbsolutePath)))
            {
                return false;
            }

            foreach (var input in expectedInputs.Where(static input => input.Kind == CookSnapshotInputKind.File))
            {
                if (!streams.ContainsKey(input.SourcePath))
                {
                    streams.Add(input.SourcePath, new(input.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous));
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
}
