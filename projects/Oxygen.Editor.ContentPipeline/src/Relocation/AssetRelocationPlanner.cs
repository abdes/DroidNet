// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.Input;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Turns a relocation request into physical moves, an identity mapping and rewritten referrers, rejecting
/// any request that would break an import, a scene's identity or derived content.
/// </summary>
internal static class AssetRelocationPlanner
{
    /// <summary>Plans a relocation against the saved project files.</summary>
    /// <param name="project">The project.</param>
    /// <param name="request">The requested moves.</param>
    /// <param name="index">The project's current references.</param>
    /// <param name="files">Reads referrer baselines.</param>
    /// <param name="cancellationToken">Cancels planning.</param>
    /// <returns>The plan.</returns>
    /// <exception cref="AssetRelocationException">The request is not allowed; nothing was changed.</exception>
    public static async Task<AssetRelocationPlan> PlanAsync(
        ProjectContext project,
        AssetRelocationRequest request,
        AuthoredReferenceIndex index,
        IAtomicFileStore files,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(index);
        var map = new RelocationMap();
        foreach (var move in request.Moves.Where(static move => move.Source != move.Target))
        {
            AddMove(project, index, map, move);
        }

        // A file's references name the asset it stands for; a script source's name the script it cooks to. A
        // container's products are named under it, so they move with it as a folder's files do.
        foreach (var move in map.Moves)
        {
            var source = RelocationPaths.ToVirtual(project, move.Source)!;
            var target = RelocationPaths.ToVirtual(project, move.Target)!;
            map.AddPath(source, target, move.IsDirectory);
            var isContainer = !move.IsDirectory && AssetFileTypes.Find(source.Name) is { ProductExtensions.Count: > 0 };
            map.AddIdentity(move.IsDirectory ? source : AssetFileTypes.ToIdentity(source), move.IsDirectory ? target : AssetFileTypes.ToIdentity(target), move.IsDirectory || isContainer);
        }

        var renames = new Dictionary<FileSystemPath, List<InputProductRename>>();
        foreach (var rename in request.ProductRenames.Where(static rename => rename.Product != rename.Target))
        {
            AddProductRename(project, index, map, renames, rename);
        }

        var edits = await PlanEditsAsync(project, index, map, renames, files, cancellationToken).ConfigureAwait(false);
        var referrers = edits.Where(edit => !map.IsMoved(edit.OriginalPath) && !renames.ContainsKey(edit.OriginalPath))
            .Select(edit => RelocationPaths.ToVirtual(project, edit.OriginalPath)!)
            .Order().ToArray();
        return new(request, map, edits, referrers);
    }

    /// <summary>Finds the imported model a product is named under.</summary>
    /// <param name="product">The identity.</param>
    /// <returns>The model, or null when the identity is not a model's product.</returns>
    internal static AssetPath? ModelOf(AssetPath product)
        => AssetFileTypes.ContainerOf(product) is { } container && AssetFileTypes.Is(container.Name, AssetFileCategory.Model) ? container : null;

    private static void AddMove(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, AssetRelocationMove move)
    {
        var (source, target, isDirectory) = Resolve(project, move);
        if (map.Moves.Any(existing => source.IsSameOrInside(existing.Source) || existing.Source.IsSameOrInside(source)))
        {
            throw new AssetRelocationException("Select either a folder or the items inside it, not both.");
        }

        var name = move.Source.Name;
        var root = project.ProjectRoot;
        foreach (var import in index.Imports)
        {
            var folder = import.Folder;
            var model = import.PrimaryVirtualPath.Name;
            if (isDirectory)
            {
                if (source != folder && source.IsSameOrInside(folder))
                {
                    throw new AssetRelocationException($"'{name}' is part of the imported model '{model}'. Move the model's folder instead.");
                }

                continue;
            }

            if (source == import.PrimaryPath)
            {
                if (source.Parent != target.Parent)
                {
                    throw new AssetRelocationException($"'{model}' references its other files by relative path. Move the model's folder to move it.");
                }
            }
            else if (import.Files.Contains(source))
            {
                throw new AssetRelocationException($"'{name}' is part of the imported model '{model}'. Move the model's folder instead.");
            }
        }

        map.AddMove(new(source, target, isDirectory));
        if (!isDirectory)
        {
            AddTextureCompanions(project, index, map, source, target);
        }
    }

    private static (FileSystemPath Source, FileSystemPath Target, bool IsDirectory) Resolve(ProjectContext project, AssetRelocationMove move)
    {
        RequireMovable(project, move);
        if (RelocationPaths.ToPhysical(project, move.Source) is not { } source || RelocationPaths.ToPhysical(project, move.Target) is not { } target)
        {
            throw new AssetRelocationException($"'{move.Source}' cannot be moved to '{move.Target}'.");
        }

        var fileSystem = project.FileSystem;
        var isDirectory = fileSystem.Directory.Exists(source.FullPath);
        if (!isDirectory && !fileSystem.File.Exists(source.FullPath))
        {
            throw new AssetRelocationException($"'{move.Source}' no longer exists; it was deleted or moved outside the Content Explorer.");
        }

        RequireValidTarget(project, move, source, target, isDirectory);
        return (source, target, isDirectory);
    }

    private static void RequireMovable(ProjectContext project, AssetRelocationMove move)
    {
        if (ModelOf(move.Source) is { } model)
        {
            throw AssetRelocationException.ModelProduct(move.Source, model);
        }

        if (RelocationPaths.FindWritableMount(project, move.Source) is null)
        {
            throw new AssetRelocationException($"'{move.Source}' is read-only. Only project content can be renamed or moved.");
        }

        if (RelocationPaths.FindWritableMount(project, move.Target) is null)
        {
            throw new AssetRelocationException("Choose a destination under Content or another authoring folder.");
        }

        if (move.Source.IsMount)
        {
            throw new AssetRelocationException("A content mount is renamed in Mounts, not here.");
        }
    }

    private static void RequireValidTarget(ProjectContext project, AssetRelocationMove move, FileSystemPath source, FileSystemPath target, bool isDirectory)
    {
        var fileSystem = project.FileSystem;
        var name = move.Target.Name;
        if (VirtualPath.ValidateSegment(name, VirtualPathRuleSet.Authored) is { } invalid)
        {
            throw new AssetRelocationException(invalid.ToMessage());
        }

        if (isDirectory && move.Target.IsSameOrInside(move.Source))
        {
            throw new AssetRelocationException("A folder cannot be moved into itself.");
        }

        if (!isDirectory && !string.Equals(AssetFileTypes.GetSuffix(move.Source.Name), AssetFileTypes.GetSuffix(name), StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException("A rename keeps the asset's type; change only its name.");
        }

        // A change of case only is a rename of the same file.
        if (source != target && (fileSystem.File.Exists(target.FullPath) || fileSystem.Directory.Exists(target.FullPath)))
        {
            throw new AssetRelocationException($"'{name}' already exists in '{move.Target.Parent}'.");
        }

        if (!fileSystem.Directory.Exists(target.Parent!.FullPath))
        {
            throw new AssetRelocationException($"The destination folder '{move.Target.Parent}' does not exist.");
        }

        if (!isDirectory && SourceCollisions.Find(project, target, ignored: source) is [var collision, ..])
        {
            throw new AssetRelocationException(AssetRelocationException.SourceCollisionCode, $"'{collision.Name}' is already in '{move.Target.Parent}'; both would cook to the same asset. Choose another name.");
        }
    }

    private static void AddFile(ProjectContext project, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        if (project.FileSystem.File.Exists(target.FullPath) && source != target)
        {
            throw new AssetRelocationException($"'{target.Name}' already exists next to the destination.");
        }

        map.AddMove(new(source, target, IsDirectory: false));
    }

    // A texture descriptor and the image beside it are one asset: each moves with the other, and a rename
    // renames an image that shares the descriptor's name.
    private static void AddTextureCompanions(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        var fileSystem = project.FileSystem;
        var folder = source.Parent!;
        var targetFolder = target.Parent!;
        var stem = RelocationPaths.GetDisplayName(source.Name);
        var newStem = RelocationPaths.GetDisplayName(target.Name);
        FileSystemPath Companion(FileSystemPath file)
            => targetFolder.Combine(string.Equals(RelocationPaths.GetDisplayName(file.Name), stem, FileSystemPath.NameComparison)
                ? newStem + AssetFileTypes.GetSuffix(file.Name)
                : file.Name);

        if (source.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal))
        {
            var content = fileSystem.File.ReadAllBytes(source.FullPath);
            foreach (var image in AuthoredReferenceFormat.ReadTextureSources(project, source, content)
                .Where(image => image.Parent == folder && fileSystem.File.Exists(image.FullPath) && !map.IsMoved(image)))
            {
                AddFile(project, map, image, Companion(image));
            }

            return;
        }

        foreach (var descriptor in index.Descriptors.Where(descriptor => descriptor.Location.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal)
            && descriptor.Location.Parent == folder && !map.IsMoved(descriptor.Location)))
        {
            if (AuthoredReferenceFormat.ReadTextureSources(project, descriptor.Location, fileSystem.File.ReadAllBytes(descriptor.Location.FullPath)).Contains(source))
            {
                AddFile(project, map, descriptor.Location, Companion(descriptor.Location));
            }
        }
    }

    // An action or a context is renamed inside its source: the source is rewritten, and the product's new identity
    // retargets every reference to it.
    private static void AddProductRename(
        ProjectContext project,
        AuthoredReferenceIndex index,
        RelocationMap map,
        Dictionary<FileSystemPath, List<InputProductRename>> renames,
        InputProductRename rename)
    {
        if (ModelOf(rename.Product) is { } model)
        {
            throw AssetRelocationException.ModelProduct(rename.Product, model);
        }

        if (AssetFileTypes.ContainerOf(rename.Product) is not { } container || RelocationPaths.FindWritableMount(project, rename.Product) is null)
        {
            throw new AssetRelocationException($"'{rename.Product}' is not an action or a mapping context of the project's input sources.");
        }

        var file = InputSourceFiles.FileOf(project, container) is { } source && project.FileSystem.File.Exists(source.FullPath)
            ? source
            : throw new AssetRelocationException($"The input source of '{rename.Product.Name}' no longer exists.");
        if (map.IsMoved(file) || !index.Descriptors.Any(descriptor => descriptor.Location == file))
        {
            throw new AssetRelocationException($"Rename '{rename.CurrentName}' on its own, not while its input source moves.");
        }

        var pending = renames.TryGetValue(file, out var existing) ? existing : renames[file] = [];
        try
        {
            var content = project.FileSystem.File.ReadAllBytes(file.FullPath);
            foreach (var earlier in pending)
            {
                content = AuthoredReferenceFormat.RenameInputProduct(content, earlier);
            }

            _ = AuthoredReferenceFormat.RenameInputProduct(content, rename);
        }
        catch (AssetFormatException error)
        {
            throw new AssetRelocationException(error.Problem.Message, error);
        }

        pending.Add(rename);
        map.AddIdentity(rename.Product, rename.Target, isFolder: false);
    }

    private static async Task<IReadOnlyList<RelocationEdit>> PlanEditsAsync(
        ProjectContext project,
        AuthoredReferenceIndex index,
        RelocationMap map,
        Dictionary<FileSystemPath, List<InputProductRename>> renames,
        IAtomicFileStore files,
        CancellationToken cancellationToken)
    {
        var edits = new List<RelocationEdit>();
        foreach (var descriptor in index.Descriptors)
        {
            var snapshot = await files.ReadAsync(descriptor.Location.FullPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            var renamed = content;
            foreach (var rename in renames.GetValueOrDefault(descriptor.Location) ?? [])
            {
                renamed = AuthoredReferenceFormat.RenameInputProduct(renamed, rename);
            }

            if ((AuthoredReferenceFormat.RewriteDescriptor(descriptor.Location, renamed, map)
                ?? (ReferenceEquals(renamed, content) ? null : renamed)) is { } rewritten)
            {
                edits.Add(new(descriptor.Location, map.MapPath(descriptor.Location), snapshot.Version, content, rewritten));
            }
        }

        foreach (var import in index.Imports)
        {
            // The record stays where it is, named by the model's source identity; its source and virtual path follow
            // the model, so its products are named under the model's new path.
            var target = map.MapPath(import.PrimaryPath);
            var folder = target.Parent!;

            // The primary may be renamed in its folder; the files it reads must keep their place beside it.
            if (import.Files.Any(file => file != import.PrimaryPath && !string.Equals(
                Path.GetRelativePath(import.Folder.FullPath, file.FullPath),
                Path.GetRelativePath(folder.FullPath, map.MapPath(file).FullPath),
                StringComparison.OrdinalIgnoreCase)))
            {
                throw new AssetRelocationException($"The move would separate '{import.PrimaryVirtualPath.Name}' from its files. Move the model's folder instead.");
            }

            // Paths compare without case on Windows; a rename that changes only case still renames the model.
            if (string.Equals(target.FullPath, import.PrimaryPath.FullPath, StringComparison.Ordinal))
            {
                continue;
            }

            var snapshot = await files.ReadAsync(import.RecordPath.FullPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            var virtualPath = RelocationPaths.ToVirtual(project, target)
                ?? throw new AssetRelocationException($"'{import.PrimaryVirtualPath.Name}' would leave the project's authoring mounts.");
            var rewritten = (ModelImportSettings.Parse(import.RecordPath.FullPath, content)
                .WithSource(import.RecordPath.FullPath, target.FullPath) with
            { VirtualPath = virtualPath.VirtualPath }).ToBytes();
            edits.Add(new(import.RecordPath, import.RecordPath, snapshot.Version, content, rewritten));
        }

        return edits;
    }
}
