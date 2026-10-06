// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Owns explicit source retention, settings and cooking as one visible operation.</summary>
public sealed partial class ContentPipelineService
{
    private readonly DroidNet.Storage.IAtomicFileStore importSettingsFiles = files;

    /// <inheritdoc />
    public Task<ContentCookResult> ImportSourceAsync(SceneImportRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var current = request;
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, new Uri(Path.GetFullPath(request.SourcePath))) { Import = request, OriginContext = request.Project },
            (operation, token) => this.ImportSourceCoreAsync(operation, current, updated => current = updated, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> ReimportSourceAsync(Uri sourceUri, ProjectContext expectedProject, CancellationToken cancellationToken)
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
        Uri? retainedUri = null;
        try
        {
            if (!ReferenceEquals(operation.Project, request.Project))
            {
                throw new InvalidOperationException("The project changed. Review the import destination again.");
            }

            if (request.Replacement is { } replacement)
            {
                retainedUri = replacement.SourceUri;
                return await this.CookReplacementAsync(operation, request, retainRecovery, cancellationToken).ConfigureAwait(false);
            }

            var target = SceneImportTarget.Resolve(operation.Project, request.DestinationFolder, request.Name);
            var retained = request.RetainedSource ?? await this.RetainRequestedSourceAsync(operation, request, target, cancellationToken).ConfigureAwait(false);
            var primary = Path.GetFullPath(Path.Combine(operation.Project.ProjectRoot, retained.DirectoryRelativePath, retained.PrimaryRelativePath));
            retainedUri = CookInputResolver.FindAuthoringSourceUri(operation.Project, primary)
                ?? throw new InvalidDataException("The retained source is outside the project's authoring mounts.");
            var recovery = request with { RetainedSource = retained };
            retainRecovery(recovery);
            CookRunContext.Report(new(Message: "Source retained. Preparing import settings.")
            {
                RecoveryRequest = new(CookTargetKind.Asset, retainedUri) { Import = recovery, OriginContext = operation.Project },
            });
            await this.CreateImportSettingsAsync(operation, retained, target, request.Name, request.Provenance, cancellationToken).ConfigureAwait(false);
            CookRunContext.Report(new(Message: "Import settings saved. Cooking retained source.")
            {
                RecoveryRequest = new(CookTargetKind.Asset, retainedUri) { IsReimport = true, OriginContext = operation.Project },
            });
            var cooked = await this.CookAssetCoreAsync(operation, retainedUri, cancellationToken).ConfigureAwait(false);
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

    private async Task<RetainedImportSource> RetainRequestedSourceAsync(ContentCookOperation operation, SceneImportRequest request, SceneImportTarget target, CancellationToken cancellationToken)
    {
        var source = Path.GetFullPath(request.SourcePath);
        var input = new ContentCookInput(new Uri(source), ContentCookAssetKind.ForeignSource, target.MountName,
            Path.GetFileName(source), source, OutputVirtualPath: null, ContentCookInputRole.Primary);
        var recipe = this.manifestBuilder.BuildModelJob(input, [], request.Name, target.CreateLayout(source), request.Provenance);
        var discovery = new SceneImportSourceDiscovery(cookDocuments, this.cookCoordinator, this.engineContentPipelineApi);
        if (CookInputResolver.FindAuthoringSourceUri(operation.Project, source) is not null)
        {
            var discovered = await discovery.DiscoverAsync(operation, source, recipe, cancellationToken).ConfigureAwait(false);
            var primary = discovered.Files.Single(file => string.Equals(file.SourcePath, source, StringComparison.OrdinalIgnoreCase));
            var bundleRoot = source[..^primary.RelativePath.Replace('/', Path.DirectorySeparatorChar).Length].TrimEnd(Path.DirectorySeparatorChar);
            return new(
                Path.GetRelativePath(operation.Project.ProjectRoot, bundleRoot).Replace('\\', '/'),
                discovered.PrimaryRelativePath,
                discovered.Files.Where(static file => file.Kind == Oxygen.Editor.ContentPipeline.Snapshots.CookSnapshotInputKind.File).Select(static file => new RetainedImportSourceFile(file.RelativePath, file.DiscoveryHash)).ToImmutableArray());
        }

        var result = await new ImportSourceRetention(cookDocuments, this.cookCoordinator).RetainAsync(
            operation, request.Name, token => discovery.DiscoverAsync(operation, source, recipe, token), cancellationToken).ConfigureAwait(false);
        return result.Source ?? (result.NeedsSave.IsEmpty
            ? throw new IOException("Reload changed source before importing.")
            : throw new CookInputsNeedSaveException(result.NeedsSave));
    }

    private async Task CreateImportSettingsAsync(ContentCookOperation operation, RetainedImportSource source, SceneImportTarget target, string name, NativeMaterialSlotProvenance provenance, CancellationToken cancellationToken)
    {
        this.cookCoordinator.VerifyWriter(operation);
        var settings = NativeSceneImportSettings.Create(source, target.MountName, name, target.OutputDirectory) with { MaterialSlotProvenance = provenance };
        var path = settings.ResolveFile(operation.Project.ProjectRoot, settings.PrimaryRelativePath) + NativeSceneImportSettings.SidecarSuffix;
        using var reads = await cookDocuments.AcquireAsync([path], cancellationToken).ConfigureAwait(false);
        if (reads.Documents.Any(static document => document.IsDirty))
        {
            throw new CookInputsNeedSaveException(reads.Documents.Where(static document => document.IsDirty));
        }

        var existing = await this.importSettingsFiles.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        var bytes = settings.ToBytes();
        if (existing.Version.Exists)
        {
            if (!existing.Content.AsSpan().SequenceEqual(bytes))
            {
                throw new InvalidOperationException("This source already has import settings. Use Reimport to keep its existing destination and asset identities.");
            }

            return;
        }

        this.cookCoordinator.VerifyWriter(operation);
        _ = await this.importSettingsFiles.WriteAsync(path, bytes, existing.Version, cancellationToken).ConfigureAwait(false);
    }
}
