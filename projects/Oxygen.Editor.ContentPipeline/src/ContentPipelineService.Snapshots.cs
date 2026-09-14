// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json.Nodes;
using Oxygen.Editor.ContentPipeline.Cooking;
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

    private static async Task<PreparedInput> PrepareCapturedGeometryAsync(ContentCookScope scope, ContentCookInput input, CancellationToken cancellationToken)
    {
        var snapshot = scope.Snapshot ?? throw new InvalidOperationException("Geometry preparation requires captured input.");
        var bytes = await File.ReadAllTextAsync(input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
        var descriptor = JsonNode.Parse(bytes) ?? throw new InvalidDataException("The captured geometry descriptor is empty.");
        var relative = Path.Combine(".pipeline", "Geometry", input.SourceRelativePath).Replace('\\', '/');
        var output = Path.Combine(scope.InputRoot, relative);
        if (descriptor["buffers"] is JsonArray buffers)
        {
            var originalDirectory = Path.GetDirectoryName(Path.Combine(scope.Project.ProjectRoot, input.SourceRelativePath))!;
            foreach (var buffer in buffers)
            {
                var uri = buffer!["uri"]!.GetValue<string>();
                var originalPath = Path.GetFullPath(Path.IsPathRooted(uri) ? uri : Path.Combine(originalDirectory, uri));
                var captured = snapshot.Inputs.FirstOrDefault(file => !file.IsAbsent && string.Equals(file.SourcePath, originalPath, StringComparison.OrdinalIgnoreCase))
                    ?? throw new InvalidDataException($"Geometry buffer '{uri}' was not included in the captured dependency set.");
                buffer["uri"] = Path.GetRelativePath(Path.GetDirectoryName(output)!, Path.Combine(snapshot.InputRoot, captured.RelativePath)).Replace('\\', '/');
            }
        }

        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        await File.WriteAllTextAsync(output, descriptor.ToJsonString(), cancellationToken).ConfigureAwait(false);
        return new(input with { SourceRelativePath = relative, SourceAbsolutePath = output, Role = ContentCookInputRole.GeneratedDescriptor }, []);
    }

    private async Task<ContentCookResult> CookCapturedScopesAsync(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        CancellationToken cancellationToken)
    {
        await this.publication.RecoverBeforeCookAsync(operation.Project, cancellationToken).ConfigureAwait(false);
        Incremental.CookProvenance previous;
        Import.ImportedSourceIndex imports;
        ContentCookInput[] primaryInputs;
        try
        {
            (previous, imports, resolveScopes) = await this.ResolveImportOwnershipAsync(operation, resolveScopes, cancellationToken).ConfigureAwait(false);
            primaryInputs = resolveScopes().SelectMany(static scope => scope.Inputs).ToArray();
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
        {
            return CreateFailedCook(operation, targetKind, failure);
        }

        if (await this.ValidatePrimaryInputsAsync(operation, targetKind, primaryInputs, cancellationToken).ConfigureAwait(false) is { } preflight)
        {
            return preflight;
        }

        var compatibility = await this.nativeCompatibility.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, compatibility.Diagnostics, [], Inspection: null, Validation: null);
        }

        var artifacts = compatibility.Artifacts!;
        Task? retainedDrain = null;
        try
        {
            return await this.ExecuteIncrementalCookAsync(operation, resolveScopes, targetKind, artifacts, previous, imports, cancellationToken).ConfigureAwait(false);
        }
        catch (CookInputDiscoveryException failure)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, failure.Diagnostics), [], Inspection: null, Validation: null);
        }
        catch (ContentPipelineTerminationException failure)
        {
            retainedDrain = ReleaseArtifactsAfterDrainAsync(failure.DrainCompletion, artifacts);
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

    private async Task<ContentCookResult?> ValidatePrimaryInputsAsync(ContentCookOperation operation, CookTargetKind targetKind, ContentCookInput[] primaryInputs, CancellationToken cancellationToken)
    {
        if (primaryInputs.Length == 0)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);
        }

        await this.RequireSavedDocumentsAsync(primaryInputs, cancellationToken).ConfigureAwait(false);
        var missing = CreateSourceMissingDiagnostics(operation.OperationId, primaryInputs);
        return missing.Count == 0 ? null : new(operation.OperationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operation.OperationId, missing), [], Inspection: null, Validation: null);
    }

    private async Task<(Incremental.CookProvenance previous, Import.ImportedSourceIndex imports, Func<IReadOnlyList<ContentCookScope>> scopes)> ResolveImportOwnershipAsync(
        ContentCookOperation operation, Func<IReadOnlyList<ContentCookScope>> scopes, CancellationToken cancellationToken)
    {
        var (previous, _) = await this.provenanceStore.ReadAsync(operation.Project, cancellationToken).ConfigureAwait(false);
        if (!await this.publication.HasCommittedMetadataAsync(operation.Project, cancellationToken).ConfigureAwait(false))
        {
            previous = new(1, operation.Project.ProjectId, [], []);
        }

        var imports = await Import.ImportedSourceIndex.ReadAsync(operation.Project, cookDocuments, previous, cancellationToken).ConfigureAwait(false);
        return (previous, imports, () => scopes().Select(imports.ResolveScope).ToArray());
    }

    private async Task<(CookInputSnapshot snapshot, CookDependencyGraph graph)> CaptureScopesAsync(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        Oxygen.Managed.Core.Compatibility.NativeArtifactLease artifacts,
        Oxygen.Editor.ContentPipeline.Incremental.CookProvenance previous,
        Import.ImportedSourceIndex imports,
        CancellationToken cancellationToken)
    {
        CookDependencyGraph? graph = null;
        var capture = new CookInputSnapshotCapture(cookDocuments, this.cookCoordinator);
        var captured = await capture.CaptureAsync(
            operation,
            async token =>
            {
                var scopes = resolveScopes();
                var inputs = scopes.SelectMany(static scope => scope.Inputs).ToArray();
                graph = await new CookDependencyDiscovery(
                    cookDocuments,
                    importedSources: previous.Products.Where(static product => product.ImportedSource is not null).ToDictionary(static product => product.SourceUri, static product => product.ImportedSource!),
                    discoverImported: (input, queryToken) => this.DiscoverChangedImportedSourceAsync(operation, input, artifacts, queryToken),
                    resolveImported: uri => imports.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency))
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

    private async Task<bool> InputsAreCurrentAsync(
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CancellationToken cancellationToken)
    {
        using var reads = await cookDocuments.AcquireAsync(snapshot.Inputs.Select(static input => input.SourcePath), cancellationToken).ConfigureAwait(false);
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

            foreach (var input in snapshot.Inputs.Where(static input => !input.IsAbsent))
            {
                if (!streams.ContainsKey(input.SourcePath))
                {
                    streams.Add(input.SourcePath, new(input.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous));
                }
            }

            foreach (var input in snapshot.Inputs)
            {
                if (input.IsAbsent)
                {
                    if (CookSavedSourceReader.Exists(input.SourcePath))
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
