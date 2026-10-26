// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Owns explicit source retention, model import records and cooking as one visible operation.</summary>
public sealed partial class ContentPipelineService
{
    private readonly DroidNet.Storage.IAtomicFileStore importRecordFiles = files;

    /// <inheritdoc />
    public Task<ContentCookResult> ImportSourceAsync(SceneImportRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var current = request;
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, ScopeUri: null) { Import = request, OriginContext = request.Project },
            (operation, token) => this.ImportSourceCoreAsync(operation, current, updated => current = updated, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<ContentCookResult>> ImportSourcesAsync(IReadOnlyList<SceneImportRequest> requests, ImportRunSettings run, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(requests);
        ArgumentNullException.ThrowIfNull(run);
        if (requests.Count == 0 || requests.Any(request => !ReferenceEquals(request.Project, requests[0].Project)))
        {
            throw new ArgumentException("One import run imports at least one model, all of one project.", nameof(requests));
        }

        var results = new List<ContentCookResult>();

        // A model already imported in place keeps its record, which takes the chosen settings; it is imported again.
        var project = requests[0].Project;
        var reimported = requests.Where(static request => request.Replacement is null)
            .Select(request => (Request: request, Record: InPlaceRecord(project, request)))
            .Where(static entry => entry.Record is not null)
            .ToArray();
        SceneImportRequest[] added =
        [
            .. requests.Where(request => request.Replacement is null && !reimported.Any(entry => ReferenceEquals(entry.Request, request)))
                .Select(request => request with { Run = run }),
        ];
        if (added.Length == 1)
        {
            results.Add(await this.ImportSourceAsync(added[0], cancellationToken).ConfigureAwait(false));
        }
        else if (added.Length > 1)
        {
            results.Add(await this.cookCoordinator.RunCookAsync(
                new(CookTargetKind.Changes, ScopeUri: null) { Assets = [.. added.Select(request => PlannedIdentity(project, request))], OriginContext = project },
                (operation, token) => this.ImportNewSourcesCoreAsync(operation, added, run, token),
                cancellationToken).ConfigureAwait(false));
        }

        foreach (var (request, record) in reimported)
        {
            results.Add(await this.ReimportInPlaceAsync(request, record!, run, cancellationToken).ConfigureAwait(false));
        }

        // A replacement's capture and commit belong to its model alone, so it runs on its own, after the new models.
        foreach (var replacement in requests.Where(static request => request.Replacement is not null))
        {
            results.Add(await this.ImportSourceAsync(replacement with { Run = run }, cancellationToken).ConfigureAwait(false));
        }

        return results;
    }

    /// <inheritdoc />
    public Task<ContentCookResult> ReimportSourceAsync(AssetPath sourceUri, ProjectContext expectedProject, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sourceUri);
        ArgumentNullException.ThrowIfNull(expectedProject);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, sourceUri) { IsReimport = true, OriginContext = expectedProject, CoalescePending = true },
            (operation, token) => ReferenceEquals(operation.Project, expectedProject)
                ? this.CookAssetCoreAsync(operation, sourceUri, token)
                : Task.FromResult(CreateFailedCook(operation, CookTargetKind.Asset, new InvalidOperationException("The project changed before reimport started."))),
            cancellationToken);
    }

    private async Task<ContentCookResult> ImportSourceCoreAsync(ContentCookOperation operation, SceneImportRequest request, Action<SceneImportRequest> retainRecovery, CancellationToken cancellationToken)
    {
        AssetPath? retainedUri = null;
        try
        {
            if (!ReferenceEquals(operation.Project, request.Project))
            {
                throw new InvalidOperationException("The project changed. Review the import destination again.");
            }

            if (request.Replacement is { } replacement)
            {
                retainedUri = replacement.SourceUri;
                await this.SaveReplacementSettingsAsync(operation, replacement, request.Settings, cancellationToken).ConfigureAwait(false);
                return await this.CookReplacementAsync(operation, request, retainRecovery, cancellationToken).ConfigureAwait(false);
            }

            var retained = request.RetainedSource ?? await this.RetainRequestedSourceAsync(operation, request, cancellationToken).ConfigureAwait(false);
            var primary = operation.Project.ProjectRoot.Combine(Path.Combine(retained.DirectoryRelativePath, retained.PrimaryRelativePath));
            retainedUri = CookInputResolver.FindAuthoringSourceUri(operation.Project, primary)
                ?? throw new InvalidDataException("The retained source is outside the project's authoring mounts.");
            var recovery = request with { RetainedSource = retained };
            retainRecovery(recovery);
            CookRunContext.Report(new(Message: "Source retained. Preparing import settings.")
            {
                RecoveryRequest = new(CookTargetKind.Asset, retainedUri) { Import = recovery, OriginContext = operation.Project },
            });
            await this.CreateImportRecordAsync(operation, primary, retainedUri, request, cancellationToken).ConfigureAwait(false);
            CookRunContext.Report(new(Message: "Import settings saved. Cooking retained source.")
            {
                RecoveryRequest = new(CookTargetKind.Asset, retainedUri) { IsReimport = true, OriginContext = operation.Project },
            });
            var cooked = await this.CookImportedSourcesAsync(operation, [retainedUri], request.Run, cancellationToken).ConfigureAwait(false);
            return cooked with { RetainedSourceUri = retainedUri };
        }
        catch (CookInputDiscoveryException failure)
        {
            return new(operation.OperationId, CookTargetKind.Asset, OperationStatus.Failed, failure.Diagnostics, [], Inspection: null, Validation: null) { RetainedSourceUri = retainedUri };
        }
        catch (NativeCompatibilityException failure)
        {
            return new(operation.OperationId, CookTargetKind.Asset, OperationStatus.Failed, failure.Diagnostics, [], Inspection: null, Validation: null) { RetainedSourceUri = retainedUri };
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or InvalidOperationException or ArgumentException or UnauthorizedAccessException)
        {
            return CreateFailedCook(operation, CookTargetKind.Asset, failure) with { RetainedSourceUri = retainedUri };
        }
    }

    private async Task<RetainedImportSource> RetainRequestedSourceAsync(ContentCookOperation operation, SceneImportRequest request, CancellationToken cancellationToken)
    {
        // A model already under an authoring mount is used where it is; one from outside is copied, with its bundle,
        // into a folder of the chosen destination. Either way its identity is its file's virtual path.
        var source = Path.GetFullPath(request.SourcePath);
        var inProject = CookInputResolver.FindAuthoringSourceUri(operation.Project, FileSystemPath.From(source));
        var identity = inProject ?? request.DestinationFolder.Combine(request.Name + "/" + Path.GetFileName(source));
        var recipe = this.manifestBuilder.BuildModelJob(Path.GetFileName(source), [], request.Name, identity);
        var discovery = new SceneImportSourceDiscovery(this.cookCoordinator, this.engineContentPipelineApi);
        if (inProject is not null)
        {
            var discovered = await discovery.DiscoverAsync(operation, source, recipe, cancellationToken).ConfigureAwait(false);
            var primary = discovered.Files.Single(file => file.SourcePath == FileSystemPath.From(source));
            var bundleRoot = source[..^primary.RelativePath.Replace('/', Path.DirectorySeparatorChar).Length].TrimEnd(Path.DirectorySeparatorChar);
            return new(
                Path.GetRelativePath(operation.Project.ProjectRoot.FullPath, bundleRoot).Replace('\\', '/'),
                discovered.PrimaryRelativePath,
                discovered.Files.Where(static file => file.Kind == Oxygen.Editor.ContentPipeline.Snapshots.CookSnapshotInputKind.File).Select(static file => new RetainedImportSourceFile(file.RelativePath, file.DiscoveryHash)).ToImmutableArray());
        }

        return await new ImportSourceRetention(this.cookCoordinator).RetainAsync(
            operation, request.DestinationFolder, request.Name, token => discovery.DiscoverAsync(operation, source, recipe, token), cancellationToken).ConfigureAwait(false);
    }

    private async Task CreateImportRecordAsync(ContentCookOperation operation, FileSystemPath primary, AssetPath identity, SceneImportRequest request, CancellationToken cancellationToken)
    {
        this.cookCoordinator.VerifyWriter(operation);
        var project = operation.Project;
        if (ModelRecordLookup.TryFind(project, primary) is { } existing)
        {
            // A retry finds the record its first attempt wrote.
            if (!string.Equals(existing.Settings.VirtualPath, identity.VirtualPath, StringComparison.Ordinal))
            {
                throw new InvalidOperationException("This source already has an import record. Use Reimport to keep its existing destination and asset identities.");
            }

            return;
        }

        var path = ModelImportSettings.PathFor(project.ProjectRoot.FullPath, request.SourceIdentity);
        var record = ModelImportSettings.Create(path, primary.FullPath, identity.VirtualPath, request.Settings ?? ModelImportSettings.DefaultSettings(request.Name));
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        this.cookCoordinator.VerifyWriter(operation);
        _ = await this.importRecordFiles.WriteAsync(path, record.ToBytes(), DroidNet.Storage.FileVersion.Missing, cancellationToken).ConfigureAwait(false);
    }

    // The identity a new model takes: its own path when it is already under an authoring mount, else its file in a
    // folder named for it in the chosen destination.
    private static AssetPath PlannedIdentity(ProjectContext project, SceneImportRequest request)
    {
        var source = Path.GetFullPath(request.SourcePath);
        return CookInputResolver.FindAuthoringSourceUri(project, FileSystemPath.From(source))
            ?? request.DestinationFolder.Combine(request.Name + "/" + Path.GetFileName(source));
    }

    private Task<ContentCookResult> CookImportedSourcesAsync(ContentCookOperation operation, IReadOnlyList<AssetPath> sources, ImportRunSettings run, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(
            operation,
            () => [.. sources.Select(uri => this.CreateAssetScope(operation.Project, uri, allowImportedSourceChanges: true, isAutomatic: false) with { Run = run })],
            sources.Count == 1 ? CookTargetKind.Asset : CookTargetKind.Changes,
            cancellationToken);

    // Retains each new model and writes its record, then cooks every retained one together. A model that cannot be
    // retained fails alone; the others still import.
    private async Task<ContentCookResult> ImportNewSourcesCoreAsync(ContentCookOperation operation, IReadOnlyList<SceneImportRequest> requests, ImportRunSettings run, CancellationToken cancellationToken)
    {
        var retained = new List<AssetPath>();
        var failures = new List<ContentCookResult>();
        foreach (var request in requests)
        {
            var (uri, failure) = await this.TryRetainNewSourceAsync(operation, request, cancellationToken).ConfigureAwait(false);
            if (uri is not null)
            {
                retained.Add(uri);
            }
            else
            {
                failures.Add(failure!);
            }
        }

        if (retained.Count == 0)
        {
            return MergeProjectResults(operation.OperationId, failures) with { TargetKind = CookTargetKind.Changes };
        }

        CookRunContext.Report(new(Message: "Import settings saved. Cooking retained sources.")
        {
            RecoveryRequest = new(CookTargetKind.Changes, ScopeUri: null) { Assets = [.. retained], IsReimport = true, OriginContext = operation.Project },
        });
        var cooked = await this.CookImportedSourcesAsync(operation, retained, run, cancellationToken).ConfigureAwait(false);
        return failures.Count == 0 ? cooked : MergeProjectResults(operation.OperationId, [cooked, .. failures]) with { TargetKind = CookTargetKind.Changes };
    }

    // Retains one new model and writes its record; returns its identity, or the failure when it cannot.
    private async Task<(AssetPath? Uri, ContentCookResult? Failure)> TryRetainNewSourceAsync(ContentCookOperation operation, SceneImportRequest request, CancellationToken cancellationToken)
    {
        try
        {
            if (!ReferenceEquals(operation.Project, request.Project))
            {
                throw new InvalidOperationException("The project changed. Review the import destination again.");
            }

            var retained = await this.RetainRequestedSourceAsync(operation, request, cancellationToken).ConfigureAwait(false);
            var primary = operation.Project.ProjectRoot.Combine(Path.Combine(retained.DirectoryRelativePath, retained.PrimaryRelativePath));
            var uri = CookInputResolver.FindAuthoringSourceUri(operation.Project, primary)
                ?? throw new InvalidDataException("The retained source is outside the project's authoring mounts.");
            await this.CreateImportRecordAsync(operation, primary, uri, request, cancellationToken).ConfigureAwait(false);
            return (uri, null);
        }
        catch (CookInputDiscoveryException failure)
        {
            return (null, new(operation.OperationId, CookTargetKind.Changes, OperationStatus.Failed, failure.Diagnostics, [], Inspection: null, Validation: null));
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or InvalidOperationException or ArgumentException or UnauthorizedAccessException)
        {
            return (null, CreateFailedCook(operation, CookTargetKind.Changes, failure));
        }
    }

    // The record of a model imported in place, which a request for its source imports again; null for any other.
    private static ModelImportRecord? InPlaceRecord(ProjectContext project, SceneImportRequest request)
    {
        var source = FileSystemPath.From(Path.GetFullPath(request.SourcePath));
        return CookInputResolver.FindAuthoringSourceUri(project, source) is null ? null : ModelRecordLookup.TryFind(project, source);
    }

    // Imports a model already imported in place again: its record takes the chosen settings, then it cooks as a reimport.
    private Task<ContentCookResult> ReimportInPlaceAsync(SceneImportRequest request, ModelImportRecord record, ImportRunSettings run, CancellationToken cancellationToken)
    {
        var project = request.Project;
        var uri = CookInputResolver.FindAuthoringSourceUri(project, FileSystemPath.From(Path.GetFullPath(request.SourcePath)))
            ?? throw new InvalidDataException("The model's source is outside the project's authoring mounts.");
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, uri) { IsReimport = true, OriginContext = project },
            async (operation, token) =>
            {
                if (!ReferenceEquals(operation.Project, project))
                {
                    return CreateFailedCook(operation, CookTargetKind.Asset, new InvalidOperationException("The project changed before the import started."));
                }

                try
                {
                    await this.SaveRecordSettingsAsync(operation, record.RecordPath, request.Settings, token).ConfigureAwait(false);
                    return await this.CookImportedSourcesAsync(operation, [uri], run, token).ConfigureAwait(false);
                }
                catch (CookInputDiscoveryException failure)
                {
                    return new(operation.OperationId, CookTargetKind.Asset, OperationStatus.Failed, failure.Diagnostics, [], Inspection: null, Validation: null);
                }
                catch (Exception failure) when (failure is IOException or InvalidDataException or InvalidOperationException or ArgumentException or UnauthorizedAccessException)
                {
                    return CreateFailedCook(operation, CookTargetKind.Asset, failure);
                }
            },
            cancellationToken);
    }

    // The replaced model keeps its record, which takes the settings chosen for the replacement before it cooks, as the
    // Model Editor's Save does before its reimport; the cook captures the record as it then is.
    private Task SaveReplacementSettingsAsync(ContentCookOperation operation, SceneImportReplacement replacement, JsonElement? settings, CancellationToken cancellationToken)
        => this.SaveRecordSettingsAsync(operation, replacement.Record.RecordPath, settings, cancellationToken);

    // Writes the chosen settings into a model's record, leaving it untouched when they are the same.
    private async Task SaveRecordSettingsAsync(ContentCookOperation operation, string path, JsonElement? settings, CancellationToken cancellationToken)
    {
        if (settings is not { } chosen)
        {
            return;
        }

        this.cookCoordinator.VerifyWriter(operation);
        var snapshot = await this.importRecordFiles.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        var record = ModelImportSettings.Parse(path, snapshot.Content.AsMemory());
        var updated = (record with { Settings = chosen.Clone() }).ToBytes();
        if (!updated.AsSpan().SequenceEqual(record.ToBytes()))
        {
            this.cookCoordinator.VerifyWriter(operation);
            _ = await this.importRecordFiles.WriteAsync(path, updated, snapshot.Version, cancellationToken).ConfigureAwait(false);
        }
    }
}
