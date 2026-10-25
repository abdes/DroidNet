// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads what an instance of a source contains from the source's current cooked scene.</summary>
public sealed partial class ContentPipelineService : ISceneStructureProvider
{
    private readonly SceneStructureCache sceneStructures = new();

    /// <inheritdoc />
    async Task<SceneStructure?> ISceneStructureProvider.ReadAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        if (!ReferenceEquals(this.session.Context, project)
            || this.engineContentPipelineApi is not ICookedSceneInspector inspector
            || AssetFileTypes.SceneProductOf(source) is not { } scene)
        {
            return null;
        }

        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.session.Lifetime);
        var token = cancellation.Token;
        using var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, this.engineContent, token, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. publicationRead.ProductState.ProducedAssets]).ConfigureAwait(false);
        var read = new SceneStructureRead(project, source, scene, inspector, Path.Combine(project.ProjectRoot.FullPath, ".build", "inspection"));
        try
        {
            return libraries.FindPreferredAsset(scene) is { } library
                ? await this.ReadLibraryStructureAsync(read, libraries, library, token).ConfigureAwait(false)
                : await this.ReadProjectStructureAsync(read, publicationRead, token).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }
    }

    private static SceneStructure? VerifySceneKey(SceneStructure? structure, string expectedIndexKey)
        => structure is not null && !string.Equals(structure.Scene.Key.ToString("D"), expectedIndexKey, StringComparison.OrdinalIgnoreCase)
            ? throw new InvalidDataException("The composed scene does not match the selected source's asset identity.")
            : structure;

    private async Task<SceneStructure?> ReadLibraryStructureAsync(SceneStructureRead read, CookedLibraryReadSet libraries, CookedLibraryReadSet.PreferredLibraryAsset library, CancellationToken token)
    {
        var native = library.Asset.Cooked;
        if (native is null || native.AssetType != (byte)AssetType.Scene || native.VirtualPath is not { } virtualPath)
        {
            return null;
        }

        if (this.sceneStructures.Find(read.Project, read.Source, library.Fingerprint) is { } cached)
        {
            return cached;
        }

        await libraries.ValidateNativeAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
        var report = await read.Inspector.InspectScenesAsync(read.OperationRoot, library.CookedRoot, virtualPath.VirtualPath, token).ConfigureAwait(false);
        token.ThrowIfCancellationRequested();
        return this.Keep(read, library.Fingerprint, VerifySceneKey(report.SingleOrDefault(read.Source), native.AssetKey.ToString()));
    }

    private async Task<SceneStructure?> ReadProjectStructureAsync(SceneStructureRead read, CookPublicationReadLease publicationRead, CancellationToken token)
    {
        var status = (await this.statusReader.ReadAsync(read.Project, publicationRead, [read.Scene], token).ConfigureAwait(false)).Single();
        if (!HasCurrentOutput(status))
        {
            return null;
        }

        var matches = status.Outputs.Where(output => output.Kind == ContentCookAssetKind.Scene
            && (output.CookedAssetUri == read.Scene || output.SourceAssetUri == read.Scene)).Take(2).ToArray();
        var prior = publicationRead.ProductState;
        var owner = matches.Length == 1 ? prior.Products.SelectMany(static product => product.Outputs).SingleOrDefault(output => output.Asset == matches[0]) : null;
        var expectedRoot = owner is null ? null : prior.Roots.FirstOrDefault(root => string.Equals(root.Mount, owner.RootMount, StringComparison.Ordinal));
        if (owner is null || expectedRoot is null)
        {
            return null;
        }

        if (this.sceneStructures.Find(read.Project, read.Source, expectedRoot.IndexSha256) is { } cached)
        {
            return cached;
        }

        var selected = matches[0];
        var cookedRoot = publicationRead.FindProjectRoot(owner.RootMount)?.FullPath
            ?? throw new InvalidDataException("Scene output has no selected generation.");
        var outputs = await CookOutputReadLease.AcquireAsync(cookedRoot, token).ConfigureAwait(false);
        await using var lifetime = outputs.ConfigureAwait(false);
        var inventory = await outputs.ReadInventoryAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
        var indexed = expectedRoot.Matches(inventory) && inventory.IsValid
            ? inventory.Assets.SingleOrDefault(asset => string.Equals(asset.VirtualPath, selected.VirtualPath, StringComparison.Ordinal))
            : null;
        if (indexed is null)
        {
            return null;
        }

        CookedSceneReport report;
        try
        {
            report = await read.Inspector.InspectScenesAsync(read.OperationRoot, cookedRoot, selected.VirtualPath, token).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            // The native reader drains before the read lease releases the root's files.
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }

        _ = outputs.GetFiles();
        var latest = (await this.statusReader.ReadAsync(read.Project, publicationRead, [read.Scene], token).ConfigureAwait(false)).Single();
        token.ThrowIfCancellationRequested();
        return HasCurrentOutput(latest) && latest.Outputs.Contains(selected)
            ? this.Keep(read, expectedRoot.IndexSha256, VerifySceneKey(report.SingleOrDefault(read.Source), indexed.Key.ToString()))
            : null;
    }

    private SceneStructure? Keep(SceneStructureRead read, string indexDigest, SceneStructure? structure)
    {
        if (structure is not null)
        {
            this.sceneStructures.Store(read.Project, indexDigest, structure);
        }

        return structure;
    }

    /// <summary>One structure read: the source, the scene it instances as, and the native reader.</summary>
    private sealed record SceneStructureRead(ProjectContext Project, AssetPath Source, AssetPath Scene, ICookedSceneInspector Inspector, string OperationRoot);
}
