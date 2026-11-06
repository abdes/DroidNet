// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Includes reviewed retained-source replacement in the existing publication journal.</summary>
internal sealed partial class CookPublicationTransaction
{
    private static string CapturedSourcePath(ProjectContext project, Guid operationId, string published)
    {
        var inputs = Path.Combine(CookPublicationPaths.Operation(project.ProjectRoot.FullPath, operationId), "inputs");
        CookOutputLease.RejectReparsePoint(inputs);
        var path = inputs;
        foreach (var segment in CookPaths.StoragePathOf(CookPaths.Of(project, FileSystemPath.From(published))).Split('/'))
        {
            path = Path.Combine(path, segment);
            CookOutputLease.RejectReparsePoint(path);
        }

        return path;
    }

    private static async Task<CookPublicationJournal.SourceBundle> CaptureSourceReplacementAsync(ContentCookOperation operation, CookSourceReplacement source, ImmutableArray<CookProducedSourceFile> produced, CancellationToken cancellationToken)
    {
        var published = ImportSourceRetention.ResolveBundleRoot(operation.Project, source.BundleRoot);
        ValidateImage(source.Before);

        // The reviewed folder may hold no file: a replacement can stand in for a model whose source is missing. Its
        // record, outside the folder, names it.
        if (!source.Before.Exists)
        {
            throw new InvalidDataException("Source replacement requires the reviewed existing bundle.");
        }

        var current = await CookRootImage.CaptureAsync(published, copyTo: null, cancellationToken).ConfigureAwait(false);
        if (!source.Before.Matches(current))
        {
            throw new IOException("The model to replace changed since its replacement was chosen. Choose Replace again.");
        }

        var captured = CapturedSourcePath(operation.Project, operation.OperationId, published);
        var staged = PublicationSourcePath(operation.Project, operation.OperationId, source.BundleRoot);
        _ = await CookRootImage.CaptureAsync(captured, staged, cancellationToken).ConfigureAwait(false);
        foreach (var update in produced.Where(file => IsWithinSourceBundle(operation.Project, source.BundleRoot, file.RelativePath)))
        {
            var relative = Path.GetRelativePath(published, ProducedSourcePath(operation.Project, update));
            var path = Path.Combine(staged, relative);
            var before = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
            if (!before.AsSpan().SequenceEqual(update.Before))
            {
                throw new InvalidDataException("Produced source settings do not match the captured replacement bundle.");
            }

            await File.WriteAllBytesAsync(path, update.After, cancellationToken).ConfigureAwait(false);
        }

        var after = await CookRootImage.CaptureAsync(staged, copyTo: null, cancellationToken).ConfigureAwait(false);
        return !after.Exists || after.Files.IsEmpty
            ? throw new InvalidDataException("The replacement source bundle was not captured with the cook inputs.")
            : new(source.BundleRoot, source.Before, after);
    }

    private static string PublicationSourcePath(ProjectContext project, Guid operationId, string bundleRoot)
    {
        var bundle = ImportSourceRetention.ResolveBundleRoot(project, bundleRoot);
        var path = Path.Combine(CookPublicationPaths.Operation(project.ProjectRoot.FullPath, operationId), "source-output", Path.GetFileName(bundle));
        CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(path)!);
        CookOutputLease.RejectReparsePoint(path);
        return path;
    }

    private static bool IsWithinSourceBundle(ProjectContext project, string bundleRoot, string cookPath)
    {
        var bundle = FileSystemPath.From(ImportSourceRetention.ResolveBundleRoot(project, bundleRoot));
        var path = CookPaths.FileOf(project, cookPath);
        return path != bundle && path.IsSameOrInside(bundle);
    }

    private static string ProducedSourcePath(ProjectContext project, CookProducedSourceFile file)
    {
        if (file.Before is null || file.After is null || !CookPaths.IsValid(file.RelativePath))
        {
            throw new InvalidDataException("Produced source settings contain an invalid path or byte payload.");
        }

        var record = CookPaths.TryFileOf(project, file.RelativePath);
        var location = record is null ? null : project.FindByFilePath(record);
        if (location is not { MountKind: ProjectMountKind.Authoring } || !AssetFileTypes.Is(location.Path.Name, AssetFileCategory.ImportSettings))
        {
            throw new InvalidDataException("A publication may update only what an import establishes in its model import record.");
        }

        var before = ModelImportSettings.Parse(file.Before, location.Identity);
        var after = ModelImportSettings.Parse(file.After, location.Identity);
        if (!before.DiffersOnlyByImport(after))
        {
            throw new InvalidDataException("A publication may update only what an import establishes in its model import record.");
        }

        var primary = before.ResolveSource(project)
            ?? throw new InvalidDataException("Produced metadata names a model in a mount that is not bound.");
        _ = CookInputResolver.FindAuthoringSourceUri(project, primary)
            ?? throw new InvalidDataException("Produced metadata does not belong to a project authoring source.");
        var source = CookInputResolver.Resolve(project, location.Identity, ContentCookInputRole.Dependency);
        if (source.Kind != ContentCookAssetKind.ForeignSource
            || !AssetFileTypes.Is(primary.Name, AssetFileCategory.Model)
            || source.SourceAbsolutePath != primary)
        {
            throw new InvalidDataException("Produced metadata does not identify its supported model source.");
        }

        var path = location.MountRoot.FullPath;
        foreach (var segment in location.File.GetPathBelow(location.MountRoot).Split('/'))
        {
            path = Path.Combine(path, segment);
            CookOutputLease.RejectReparsePoint(path);
        }

        return path;
    }

    private async Task RestoreSourceFilesAsync()
    {
        foreach (var update in this.journal.SourceFiles.Reverse())
        {
            var path = ProducedSourcePath(this.project, update);
            var current = await this.files.ReadAsync(path, CancellationToken.None).ConfigureAwait(false);
            if (current.Version == Version(update.Before))
            {
                continue;
            }

            if (current.Version != Version(update.After))
            {
                throw new DroidNet.Storage.StorageWriteConflictException($"Retained source settings changed outside publication: '{path}'.");
            }

            _ = await this.files.WriteAsync(path, update.Before, current.Version, CancellationToken.None).ConfigureAwait(false);
        }
    }

    private IEnumerable<PublicationDirectory> Directories()
    {
        if (this.journal.SourceReplacement is { } source)
        {
            var published = ImportSourceRetention.ResolveBundleRoot(this.project, source.BundleRoot);
            var name = Path.GetFileName(published);
            yield return new(
                "Source:" + name,
                source.Before,
                source.After,
                published,
                PublicationSourcePath(this.project, this.journal.OperationId, source.BundleRoot),
                this.RootPath("previous-source", name),
                this.RootPath("discarded-source", name));
        }
    }

    private sealed record PublicationDirectory(string Name, CookRootImage Before, CookRootImage After, string Published, string Staged, string Previous, string Discarded);
}
