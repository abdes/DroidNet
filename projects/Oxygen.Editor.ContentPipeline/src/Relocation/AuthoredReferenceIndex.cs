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
        foreach (var path in mounts.SelectMany(mount => EnumerateFiles(project.FileSystem, mount.Location!)))
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
                var identity = AssetFileTypes.ToIdentity(virtualPath);
                descriptors.Add(new(path, virtualPath, identity, AuthoredReferenceFormat.ReadReferences(path, identity, content))
                {
                    Refused = AuthoredReferenceFormat.ReadRefusedReferences(path, identity, content),
                });
            }
            catch (Exception error) when (error is JsonException or AssetFormatException or InvalidDataException or InvalidOperationException)
            {
                // A file the editor cannot parse contributes no references; changes that run on the index report it.
                unreadable.Add(path);
            }
        }

        var records = new ModelImportRecords(project);
        // A record whose model is in a mount this machine has not bound is readable; it names no model here.
        unreadable.AddRange(records.Problems.Where(static problem => problem.UnboundSource is null)
            .Select(static problem => FileSystemPath.From(problem.RecordPath)));
        foreach (var record in records.All)
        {
            var primary = FileSystemPath.From(record.SourcePath);
            if (RelocationPaths.ToVirtual(project, primary) is { } virtualPath)
            {
                var recordPath = FileSystemPath.From(record.RecordPath);
                imports.Add(new(recordPath, primary, virtualPath, record.Settings)
                {
                    HasCompanionSource = RelocationPaths.ToVirtual(project, recordPath) is { } file && AssetFileTypes.CompanionsOf(file, virtualPath).Contains(virtualPath),
                });
            }
        }

        return new(descriptors, imports, unreadable);
    }

    /// <summary>Finds the imported model a path names: its identity, its record, or a source file at its identity.</summary>
    /// <param name="path">The virtual path.</param>
    /// <returns>The model, or null when no record is at that identity.</returns>
    public ImportSource? FindModel(AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        var identity = AssetFileTypes.ToIdentity(path);
        return this.Imports.FirstOrDefault(import => import.Identity == identity);
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
    /// <param name="Fields">Each reference, with the field that holds it.</param>
    internal sealed record Descriptor(FileSystemPath Location, AssetPath VirtualPath, AssetPath Identity, IReadOnlyList<AuthoredReference> Fields)
    {
        /// <summary>Gets the references the reference grammar refuses, which name nothing.</summary>
        public IReadOnlyList<RefusedReference> Refused { get; init; } = [];

        /// <summary>Gets the distinct identities the descriptor references.</summary>
        public IEnumerable<AssetPath> References => this.Fields.Select(static reference => reference.Identity).Distinct();
    }

    /// <summary>An imported model in a writable mount.</summary>
    /// <param name="RecordPath">The model's import record.</param>
    /// <param name="PrimaryPath">The model's primary source file.</param>
    /// <param name="PrimaryVirtualPath">The primary source's virtual path.</param>
    /// <param name="Settings">The record's content.</param>
    internal sealed record ImportSource(FileSystemPath RecordPath, FileSystemPath PrimaryPath, AssetPath PrimaryVirtualPath, ModelImportSettings Settings)
    {
        /// <summary>Gets the model's identity, which is where its record lives.</summary>
        public AssetPath Identity => this.Settings.Identity;

        /// <summary>Gets a value indicating whether the source belongs to the model, beside its record with its name or in
        /// a folder of that name; a source anywhere else is only referenced.</summary>
        public bool HasCompanionSource { get; init; }

        /// <summary>Gets the model's preview, beside its record.</summary>
        public FileSystemPath Preview => this.RecordPath.Parent!.Combine(AssetFileTypes.PreviewOf(this.Identity.Name));

        /// <summary>Gets the source files that move, are copied and deleted with the model.</summary>
        public IEnumerable<FileSystemPath> Companions
            => this.HasCompanionSource ? this.SourceFiles : [];

        /// <summary>Gets the source's own files: the primary and what it read, in the folder that holds it.</summary>
        public IEnumerable<FileSystemPath> SourceFiles
            => this.Files.Where(file => file.IsSameOrInside(this.Folder));

        /// <summary>Gets the model's folder: it references its other files relative to it.</summary>
        public FileSystemPath Folder => this.PrimaryPath.Parent!;

        /// <summary>Gets the files the model owns: its primary and every file its last import read.</summary>
        public IReadOnlyList<FileSystemPath> Files => this.Settings.FilesRead(this.PrimaryPath);
    }
}
