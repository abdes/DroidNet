// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text.Json;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The authored references of a project's writable authoring mounts, read from saved files. A relocation
/// always builds a fresh index inside its writer scope; browsing reuses a cached one until content changes.
/// </summary>
internal sealed class AuthoredReferenceIndex
{
    private AuthoredReferenceIndex(IReadOnlyList<Descriptor> descriptors, IReadOnlyList<ImportSource> imports, IReadOnlyList<FileSystemPath> unreadable)
    {
        this.Descriptors = descriptors;
        this.Imports = imports;
        this.Unreadable = unreadable;
    }

    /// <summary>Gets every readable descriptor with its references.</summary>
    public IReadOnlyList<Descriptor> Descriptors { get; }

    /// <summary>Gets every valid model import and its settings.</summary>
    public IReadOnlyList<ImportSource> Imports { get; }

    /// <summary>Gets the descriptors and sidecars that could not be read or parsed; their references are unknown.</summary>
    public IReadOnlyList<FileSystemPath> Unreadable { get; }

    /// <summary>Reads every authored descriptor in the project's writable mounts, and the model import records of the
    /// models in them.</summary>
    /// <param name="project">The project.</param>
    /// <param name="cancellationToken">Cancels the scan.</param>
    /// <returns>The index.</returns>
    public static async Task<AuthoredReferenceIndex> BuildAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var mounts = RelocationPaths.GetWritableMounts(project).ToArray();
        var descriptors = new List<Descriptor>();
        var imports = new List<ImportSource>();
        var unreadable = new List<FileSystemPath>();
        var root = project.ProjectRoot;
        foreach (var path in mounts.SelectMany(mount => EnumerateFiles(project.FileSystem, root.Combine(mount.RelativePath))))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var isDescriptor = AuthoredReferenceFormat.IsReferencingSource(path.FullPath);
            if (!isDescriptor || RelocationPaths.ToVirtual(project, path) is not { } virtualPath)
            {
                continue;
            }

            byte[] content;
            try
            {
                content = await project.FileSystem.File.ReadAllBytesAsync(path.FullPath, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                unreadable.Add(path);
                continue;
            }

            try
            {
                descriptors.Add(new(path, virtualPath, AssetFileTypes.ToIdentity(virtualPath), AuthoredReferenceFormat.ReadReferences(path, content)));
            }
            catch (Exception error) when (error is JsonException or AssetFormatException or InvalidDataException or InvalidOperationException)
            {
                // A file the editor cannot parse contributes no references; changes that run on the index report it.
                unreadable.Add(path);
            }
        }

        var records = new ModelImportRecords(project.FileSystem, root.FullPath);
        unreadable.AddRange(records.Problems.Select(static problem => FileSystemPath.From(problem.RecordPath)));
        foreach (var record in records.All)
        {
            var primary = FileSystemPath.From(record.SourcePath);
            if (RelocationPaths.ToVirtual(project, primary) is { } virtualPath)
            {
                imports.Add(new(FileSystemPath.From(record.RecordPath), primary, virtualPath, record.Settings));
            }
        }

        return new(descriptors, imports, unreadable);
    }

    /// <summary>Finds the descriptors that reference an asset, or any asset inside a folder.</summary>
    /// <param name="path">The asset's virtual path, its identity, or a folder.</param>
    /// <returns>The referring descriptors' virtual paths, excluding the asset itself.</returns>
    public IReadOnlyList<AssetPath> FindReferrers(AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        var identity = AssetFileTypes.ToIdentity(path);
        return [.. this.Descriptors
            .Where(descriptor => !descriptor.Identity.IsSameOrInside(identity)
                && descriptor.References.Any(reference => reference.IsSameOrInside(identity)))
            .Select(static descriptor => descriptor.VirtualPath)
            .Order()];
    }

    private static IEnumerable<FileSystemPath> EnumerateFiles(IFileSystem fileSystem, FileSystemPath root)
    {
        if (!fileSystem.Directory.Exists(root.FullPath))
        {
            return [];
        }

        var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = true };
        return fileSystem.Directory.EnumerateFiles(root.FullPath, "*", options)
            .Select(FileSystemPath.From)
            .Where(path => !path.GetPathBelow(root).Split('/').Any(static segment => segment.StartsWith('.')));
    }

    /// <summary>An authored descriptor and the identities it references.</summary>
    /// <param name="Location">The file on disk.</param>
    /// <param name="VirtualPath">Its virtual path.</param>
    /// <param name="Identity">Its asset identity.</param>
    /// <param name="References">The identities it references.</param>
    internal sealed record Descriptor(FileSystemPath Location, AssetPath VirtualPath, AssetPath Identity, IReadOnlyList<AssetPath> References);

    /// <summary>An imported model in a writable mount.</summary>
    /// <param name="RecordPath">The model's import record.</param>
    /// <param name="PrimaryPath">The model's primary source file.</param>
    /// <param name="PrimaryVirtualPath">The primary source's virtual path.</param>
    /// <param name="Settings">The record's content.</param>
    internal sealed record ImportSource(FileSystemPath RecordPath, FileSystemPath PrimaryPath, AssetPath PrimaryVirtualPath, ModelImportSettings Settings)
    {
        /// <summary>Gets the model's folder: it references its other files relative to it.</summary>
        public FileSystemPath Folder => this.PrimaryPath.Parent!;

        /// <summary>Gets the files the model owns: its primary and every file its last import read.</summary>
        public IReadOnlyList<FileSystemPath> Files
            => [this.PrimaryPath, .. (this.Settings.SourceFingerprint?.Files ?? [])
                .Select(file => FileSystemPath.From(Path.GetFullPath(Path.Combine(this.Folder.FullPath, file.Path))))];
    }
}
