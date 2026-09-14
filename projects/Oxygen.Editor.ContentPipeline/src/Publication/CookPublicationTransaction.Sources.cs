// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Includes reviewed retained-source replacement in the existing publication journal.</summary>
internal sealed partial class CookPublicationTransaction
{
    private static string StagedSourcePath(ProjectContext project, Guid operationId, string published)
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

    private static async Task<CookPublicationJournal.SourceBundle> CaptureSourceReplacementAsync(ContentCookOperation operation, CookSourceReplacement source, CancellationToken cancellationToken)
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

        var staged = StagedSourcePath(operation.Project, operation.OperationId, published);
        var after = await CookRootImage.CaptureAsync(staged, copyTo: null, cancellationToken).ConfigureAwait(false);
        return !after.Exists || after.Files.IsEmpty
            ? throw new InvalidDataException("The replacement source bundle was not captured with the cook inputs.")
            : new(source.BundleName, source.Before, after);
    }

    private IEnumerable<PublicationDirectory> Directories()
    {
        foreach (var root in this.journal.Roots)
        {
            yield return new(
                root.Mount,
                root.Before,
                root.After,
                this.RootPath("published", root.Mount),
                this.RootPath("output", root.Mount),
                this.RootPath("previous", root.Mount),
                this.RootPath("discarded", root.Mount));
        }

        if (this.journal.SourceReplacement is { } source)
        {
            var published = ImportSourceRetention.ResolveDestination(this.project, source.BundleName);
            yield return new(
                "Source:" + source.BundleName,
                source.Before,
                source.After,
                published,
                StagedSourcePath(this.project, this.journal.OperationId, published),
                this.RootPath("previous-source", source.BundleName),
                this.RootPath("discarded-source", source.BundleName));
        }
    }

    private sealed record PublicationDirectory(string Name, CookRootImage Before, CookRootImage After, string Published, string Staged, string Previous, string Discarded);
}
