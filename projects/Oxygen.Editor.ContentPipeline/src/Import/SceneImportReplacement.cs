// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>The existing retained source and byte baseline explicitly reviewed for replacement.</summary>
public sealed class SceneImportReplacement
{
    private SceneImportReplacement(AssetPath sourceUri, string bundleRoot, string primaryRelativePath, ModelImportRecord record, CookRootImage before)
    {
        this.SourceUri = sourceUri;
        this.BundleRoot = bundleRoot;
        this.PrimaryRelativePath = primaryRelativePath;
        this.Record = record;
        this.Before = before;
    }

    /// <summary>Gets the stable source identity preserved by replacement.</summary>
    public AssetPath SourceUri { get; }

    /// <summary>Gets the project-relative folder that holds the model and its files.</summary>
    public string BundleRoot { get; }

    /// <summary>Gets the model's primary source file, relative to <see cref="BundleRoot"/>.</summary>
    public string PrimaryRelativePath { get; }

    /// <summary>Gets the model's import record, which the replacement keeps.</summary>
    public ModelImportRecord Record { get; }

    /// <summary>Gets the reviewed source byte identities.</summary>
    internal CookRootImage Before { get; }

    /// <summary>Identifies one exclusively owned retained model without changing any source or output.</summary>
    /// <param name="project">The reviewed project.</param>
    /// <param name="folder">The authoring folder holding the bundle.</param>
    /// <param name="bundleName">The colliding bundle folder's name.</param>
    /// <param name="cancellationToken">Cancels read-only review.</param>
    /// <returns>The exact replacement target.</returns>
    public static async Task<SceneImportReplacement> ReviewAsync(ProjectContext project, AssetPath folder, string bundleName, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var root = ImportSourceRetention.ResolveDestination(project, folder, bundleName);
        var bundle = FileSystemPath.From(root);
        var before = await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false);
        var records = new ModelImportRecords(project.FileSystem, project.ProjectRoot.FullPath).All
            .Where(record => FileSystemPath.From(record.SourcePath) is var source && source != bundle && source.IsSameOrInside(bundle))
            .ToArray();
        if (!before.Exists || records.Length != 1)
        {
            throw new InvalidDataException("The existing folder must contain one configured model. Choose another name or import the existing source in place.");
        }

        var record = records[0];
        var primary = FileSystemPath.From(record.SourcePath).GetPathBelow(bundle);

        // The model owns what its last import read: its primary and every file beside it that the import used.
        var owned = OwnedFiles(record, bundle).ToHashSet(FileSystemPath.NameComparer);
        var unrelated = before.Files.Keys.FirstOrDefault(path => !owned.Contains(path));
        if (unrelated is not null)
        {
            throw new InvalidDataException($"The retained folder also contains '{unrelated}', which this import does not own. Move it before replacing this source.");
        }

        var after = await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false);
        if (!before.Matches(after))
        {
            throw new IOException("The retained source changed during review. Review it again.");
        }

        return new(folder.Combine(bundleName + "/" + primary), bundle.GetPathBelow(project.ProjectRoot), primary, record, before);
    }

    private static IEnumerable<string> OwnedFiles(ModelImportRecord record, FileSystemPath bundle)
    {
        var folder = Path.GetDirectoryName(record.SourcePath)!;
        return record.Settings.SourceFingerprint?.Files
            .Select(file => FileSystemPath.From(Path.GetFullPath(Path.Combine(folder, file.Path))))
            .Where(path => path != bundle && path.IsSameOrInside(bundle))
            .Select(path => path.GetPathBelow(bundle))
            .Append(FileSystemPath.From(record.SourcePath).GetPathBelow(bundle))
            ?? [FileSystemPath.From(record.SourcePath).GetPathBelow(bundle)];
    }
}
