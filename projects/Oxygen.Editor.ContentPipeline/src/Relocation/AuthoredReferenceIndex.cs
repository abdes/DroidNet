// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The authored references of a project's writable authoring mounts, read from saved files. A relocation
/// always builds a fresh index inside its writer scope; browsing reuses a cached one until content changes.
/// </summary>
internal sealed class AuthoredReferenceIndex
{
    private AuthoredReferenceIndex(IReadOnlyList<string> mounts, IReadOnlyList<Descriptor> descriptors, IReadOnlyList<ImportSource> imports, IReadOnlyList<FileSystemPath> unreadable)
    {
        this.Mounts = mounts;
        this.Descriptors = descriptors;
        this.Imports = imports;
        this.Unreadable = unreadable;
    }

    /// <summary>Gets the writable authoring mount names.</summary>
    public IReadOnlyList<string> Mounts { get; }

    /// <summary>Gets every readable descriptor with its references.</summary>
    public IReadOnlyList<Descriptor> Descriptors { get; }

    /// <summary>Gets every valid model import and its settings.</summary>
    public IReadOnlyList<ImportSource> Imports { get; }

    /// <summary>Gets the descriptors and sidecars that could not be read or parsed; their references are unknown.</summary>
    public IReadOnlyList<FileSystemPath> Unreadable { get; }

    /// <summary>Reads every authored descriptor and model import sidecar in the project's writable mounts.</summary>
    /// <param name="project">The project.</param>
    /// <param name="cancellationToken">Cancels the scan.</param>
    /// <returns>The index.</returns>
    public static async Task<AuthoredReferenceIndex> BuildAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var mounts = RelocationPaths.GetWritableMounts(project).ToArray();
        var names = mounts.Select(static mount => mount.Name).ToArray();
        var descriptors = new List<Descriptor>();
        var imports = new List<ImportSource>();
        var unreadable = new List<FileSystemPath>();
        var root = project.ProjectRoot;
        foreach (var path in mounts.SelectMany(mount => EnumerateFiles(root.Combine(mount.RelativePath))))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var isDescriptor = AuthoredReferenceFormat.IsDescriptor(path.FullPath);
            if ((!isDescriptor && !AuthoredReferenceFormat.IsImportSettings(path.FullPath)) || RelocationPaths.ToVirtual(project, path) is not { } virtualPath)
            {
                continue;
            }

            byte[] content;
            try
            {
                content = await File.ReadAllBytesAsync(path.FullPath, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                unreadable.Add(path);
                continue;
            }

            try
            {
                if (isDescriptor)
                {
                    descriptors.Add(new(path, virtualPath, AssetFileTypes.ToIdentity(virtualPath), AuthoredReferenceFormat.ReadReferences(content, names)));
                }
                else
                {
                    var settings = NativeSceneImportSettings.Parse(content);
                    var primary = FileSystemPath.From(path.FullPath[..^NativeSceneImportSettings.SidecarSuffix.Length]);
                    if (FileSystemPath.From(settings.ResolveFile(project.ProjectRoot.FullPath, settings.PrimaryRelativePath)) == primary)
                    {
                        imports.Add(new(path, primary, virtualPath.WithName(primary.Name), settings));
                    }
                }
            }
            catch (Exception error) when (error is JsonException or InvalidDataException or InvalidOperationException)
            {
                // A file the editor cannot parse contributes no references; changes that run on the index report it.
                unreadable.Add(path);
            }
        }

        return new(names, descriptors, imports, unreadable);
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

    private static IEnumerable<FileSystemPath> EnumerateFiles(FileSystemPath root)
    {
        if (!Directory.Exists(root.FullPath))
        {
            return [];
        }

        var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = true };
        return Directory.EnumerateFiles(root.FullPath, "*", options)
            .Select(FileSystemPath.From)
            .Where(path => !path.GetPathBelow(root).Split('/').Any(static segment => segment.StartsWith('.')));
    }

    /// <summary>An authored descriptor and the identities it references.</summary>
    /// <param name="Location">The file on disk.</param>
    /// <param name="VirtualPath">Its virtual path.</param>
    /// <param name="Identity">Its asset identity.</param>
    /// <param name="References">The identities it references.</param>
    internal sealed record Descriptor(FileSystemPath Location, AssetPath VirtualPath, AssetPath Identity, IReadOnlyList<AssetPath> References);

    /// <summary>A retained model import.</summary>
    /// <param name="SidecarPath">The <c>.import.json</c> file.</param>
    /// <param name="PrimaryPath">The model's primary source file.</param>
    /// <param name="PrimaryVirtualPath">The primary source's virtual path.</param>
    /// <param name="Settings">The saved import settings.</param>
    internal sealed record ImportSource(FileSystemPath SidecarPath, FileSystemPath PrimaryPath, AssetPath PrimaryVirtualPath, NativeSceneImportSettings Settings)
    {
        /// <summary>Gets the output group's identity folder under one type folder.</summary>
        /// <param name="typeFolder">One of <see cref="RelocationPaths.ImportTypeFolders"/>.</param>
        /// <returns>The folder, such as <c>/Content/Geometry/Robot</c>.</returns>
        public AssetPath GetGroupFolder(string typeFolder) => AssetPath.Create(this.Settings.MountPoint, typeFolder + "/" + this.Settings.OutputDirectory);
    }
}
