// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Includes reviewed retained-source replacement in the existing publication journal.</summary>
internal sealed partial class CookPublicationTransaction
{
    private static string CapturedSourcePath(ProjectContext project, Guid operationId, string published)
    {
        var inputs = Path.Combine(project.ProjectRoot, ".build", "cook", operationId.ToString("N"), "inputs");
        CookOutputLease.RejectReparsePoint(inputs);
        var path = inputs;
        foreach (var segment in Path.GetRelativePath(project.ProjectRoot, published).Split(Path.DirectorySeparatorChar))
        {
            path = Path.Combine(path, segment);
            CookOutputLease.RejectReparsePoint(path);
        }

        return path;
    }

    private static async Task<CookPublicationJournal.SourceBundle> CaptureSourceReplacementAsync(ContentCookOperation operation, CookSourceReplacement source, ImmutableArray<CookProducedSourceFile> produced, CancellationToken cancellationToken)
    {
        var published = ImportSourceRetention.ResolveDestination(operation.Project, source.BundleName);
        ValidateImage(source.Before);
        if (!source.Before.Exists || source.Before.Files.IsEmpty)
        {
            throw new InvalidDataException("Source replacement requires the reviewed existing bundle.");
        }

        var current = await CookRootImage.CaptureAsync(published, copyTo: null, cancellationToken).ConfigureAwait(false);
        if (!source.Before.Matches(current))
        {
            throw new IOException("The retained source changed after review. Review the replacement again.");
        }

        var captured = CapturedSourcePath(operation.Project, operation.OperationId, published);
        var staged = PublicationSourcePath(operation.Project, operation.OperationId, source.BundleName);
        _ = await CookRootImage.CaptureAsync(captured, staged, cancellationToken).ConfigureAwait(false);
        foreach (var update in produced.Where(file => IsWithinSourceBundle(operation.Project, source.BundleName, file.RelativePath)))
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
            : new(source.BundleName, source.Before, after);
    }

    private static string PublicationSourcePath(ProjectContext project, Guid operationId, string bundleName)
    {
        _ = ImportSourceRetention.ResolveDestination(project, bundleName);
        var path = Path.Combine(project.ProjectRoot, ".build", "cook", operationId.ToString("N"), "source-output", bundleName);
        CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(path)!);
        CookOutputLease.RejectReparsePoint(path);
        return path;
    }

    private static bool IsWithinSourceBundle(ProjectContext project, string bundleName, string relativePath)
    {
        var bundle = ImportSourceRetention.ResolveDestination(project, bundleName);
        return Path.GetFullPath(Path.Combine(project.ProjectRoot, relativePath)).StartsWith(bundle + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    private static string ProducedSourcePath(ProjectContext project, CookProducedSourceFile file)
    {
        if (file.Before is null || file.After is null || string.IsNullOrWhiteSpace(file.RelativePath)
            || Path.IsPathRooted(file.RelativePath) || file.RelativePath.Contains('\\', StringComparison.Ordinal)
            || file.RelativePath.Split('/').Any(static segment => segment is "" or "." or ".."))
        {
            throw new InvalidDataException("Produced source settings contain an invalid path or byte payload.");
        }

        var before = NativeSceneImportSettings.Parse(file.Before);
        var after = NativeSceneImportSettings.Parse(file.After);
        var expected = before.BundleRoot + "/" + before.PrimaryRelativePath + NativeSceneImportSettings.SidecarSuffix;
        var permitted = before with { MaterialSlotProvenance = after.MaterialSlotProvenance };
        if (!string.Equals(file.RelativePath, expected, StringComparison.Ordinal)
            || before.MaterialSlotProvenance.SourceIdentity != after.MaterialSlotProvenance.SourceIdentity
            || !permitted.ToBytes().AsSpan().SequenceEqual(after.ToBytes()))
        {
            throw new InvalidDataException("Native publication may update only its retained material-slot provenance.");
        }

        var primary = Path.GetFullPath(Path.Combine(project.ProjectRoot, before.BundleRoot, before.PrimaryRelativePath));
        var sourceUri = CookInputResolver.FindAuthoringSourceUri(project, primary)
            ?? throw new InvalidDataException("Produced metadata does not belong to a project authoring source.");
        var source = CookInputResolver.Resolve(project, sourceUri, ContentCookInputRole.Dependency);
        if (source.Kind != ContentCookAssetKind.ForeignSource
            || Path.GetExtension(primary).ToUpperInvariant() is not (".GLTF" or ".GLB" or ".FBX")
            || !string.Equals(source.SourceAbsolutePath, primary, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Produced metadata does not identify its supported model source.");
        }

        var path = project.ProjectRoot;
        foreach (var segment in file.RelativePath.Split('/'))
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
            var published = ImportSourceRetention.ResolveDestination(this.project, source.BundleName);
            yield return new(
                "Source:" + source.BundleName,
                source.Before,
                source.After,
                published,
                PublicationSourcePath(this.project, this.journal.OperationId, source.BundleName),
                this.RootPath("previous-source", source.BundleName),
                this.RootPath("discarded-source", source.BundleName));
        }
    }

    private sealed record PublicationDirectory(string Name, CookRootImage Before, CookRootImage After, string Published, string Staged, string Previous, string Discarded);
}
