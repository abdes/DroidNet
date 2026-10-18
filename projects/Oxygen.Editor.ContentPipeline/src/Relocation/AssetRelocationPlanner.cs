// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
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
        var previousGroups = new Dictionary<AssetPath, string>();
        var groupFolders = new List<string>();
        var moving = new List<AuthoredReferenceIndex.ImportSource>();
        foreach (var move in request.Moves.Where(static move => move.Source != move.Target))
        {
            AddMove(project, index, map, move);
        }

        foreach (var move in request.GroupMoves)
        {
            var import = index.Imports.FirstOrDefault(item => item.PrimaryVirtualPath == move.Source)
                ?? throw new AssetRelocationException($"'{move.Source}' is not an imported model with saved import settings.");
            ValidateGroup(move.Group);
            previousGroups[move.Source] = import.Settings.OutputDirectory;
            moving.Add(import);
            foreach (var type in RelocationPaths.ImportTypeFolders)
            {
                AddGroupFolder(project, map, groupFolders, import.GetGroupFolder(type), AssetPath.Create(import.Settings.MountPoint, type + "/" + move.Group));
            }
        }

        ExpandImportGroups(project, index, map, groupFolders, moving);
        CheckGroupOverlaps(index, map, moving);

        // A file's references name the asset it stands for; a script source's name the script it cooks to.
        foreach (var move in map.Moves)
        {
            var source = RelocationPaths.ToVirtual(project, move.Source)!;
            var target = RelocationPaths.ToVirtual(project, move.Target)!;
            map.AddPath(source, target, move.IsDirectory);
            map.AddIdentity(move.IsDirectory ? source : AssetFileTypes.ToIdentity(source), move.IsDirectory ? target : AssetFileTypes.ToIdentity(target), move.IsDirectory);
        }

        var edits = await PlanEditsAsync(project, index, map, files, cancellationToken).ConfigureAwait(false);
        var referrers = edits.Where(edit => !map.IsMoved(edit.OriginalPath))
            .Select(edit => RelocationPaths.ToVirtual(project, edit.OriginalPath)!)
            .Order().ToArray();
        return new(request, map, edits, referrers, [.. groupFolders.Distinct(StringComparer.Ordinal)], previousGroups);
    }

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
            var bundle = root.Combine(import.Settings.BundleRoot);
            var model = import.PrimaryVirtualPath.Name;
            if (isDirectory)
            {
                if (source != bundle && source.IsSameOrInside(bundle))
                {
                    throw new AssetRelocationException($"'{name}' is part of the imported model '{model}'. Move the model's folder instead.");
                }

                continue;
            }

            if (source == import.SidecarPath)
            {
                throw new AssetRelocationException($"'{name}' holds the import settings of '{model}'. Rename the model instead.");
            }

            if (source == import.PrimaryPath)
            {
                if (source.Parent != target.Parent)
                {
                    throw new AssetRelocationException($"'{model}' references its other files by relative path. Move the model's folder to move it.");
                }

                AddFile(map, import.SidecarPath, FileSystemPath.From(target.FullPath + Import.NativeSceneImportSettings.SidecarSuffix));
            }
            else if (import.Settings.Files.Any(file => FileSystemPath.From(import.Settings.ResolveFile(project.ProjectRoot.FullPath, file)) == source))
            {
                throw new AssetRelocationException($"'{name}' is part of the imported model '{model}'. Move the model's folder instead.");
            }
        }

        map.AddMove(new(source, target, isDirectory));
        if (!isDirectory)
        {
            AddTextureCompanions(index, map, source, target);
        }
    }

    private static (FileSystemPath Source, FileSystemPath Target, bool IsDirectory) Resolve(ProjectContext project, AssetRelocationMove move)
    {
        RequireMovable(project, move);
        if (RelocationPaths.ToPhysical(project, move.Source) is not { } source || RelocationPaths.ToPhysical(project, move.Target) is not { } target)
        {
            throw new AssetRelocationException($"'{move.Source}' cannot be moved to '{move.Target}'.");
        }

        var isDirectory = Directory.Exists(source.FullPath);
        if (!isDirectory && !File.Exists(source.FullPath))
        {
            throw new AssetRelocationException($"'{move.Source}' no longer exists; it was deleted or moved outside the Content Browser.");
        }

        RequireValidTarget(move, source, target, isDirectory);
        return (source, target, isDirectory);
    }

    private static void RequireMovable(ProjectContext project, AssetRelocationMove move)
    {
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

    private static void RequireValidTarget(AssetRelocationMove move, FileSystemPath source, FileSystemPath target, bool isDirectory)
    {
        var name = move.Target.Name;
        if (VirtualPath.ValidateSegment(name, VirtualPathRuleSet.Authored) is { } invalid)
        {
            throw new AssetRelocationException(invalid.ToMessage());
        }

        if (isDirectory && move.Source.Parent!.IsMount && RelocationPaths.ImportTypeFolders.Contains(move.Source.Name, StringComparer.Ordinal))
        {
            throw new AssetRelocationException($"'{move.Source.Name}' is one of the importer's fixed folders and cannot be renamed or moved.");
        }

        if (isDirectory && move.Target.IsSameOrInside(move.Source))
        {
            throw new AssetRelocationException("A folder cannot be moved into itself.");
        }

        if (!isDirectory && !string.Equals(RelocationPaths.GetExtension(move.Source.Name), RelocationPaths.GetExtension(name), StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException("A rename keeps the asset's type; change only its name.");
        }

        // A change of case only is a rename of the same file.
        if (source != target && (File.Exists(target.FullPath) || Directory.Exists(target.FullPath)))
        {
            throw new AssetRelocationException($"'{name}' already exists in '{move.Target.Parent}'.");
        }

        if (!Directory.Exists(target.Parent!.FullPath))
        {
            throw new AssetRelocationException($"The destination folder '{move.Target.Parent}' does not exist.");
        }

        if (!isDirectory && ScriptSourceNames.FindCollision(target, ignored: source) is { } collision)
        {
            throw new AssetRelocationException($"'{collision.Name}' is already in '{move.Target.Parent}'; both would cook to the same script. Choose another name.");
        }
    }

    private static void AddFile(RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        if (File.Exists(target.FullPath) && source != target)
        {
            throw new AssetRelocationException($"'{target.Name}' already exists next to the destination.");
        }

        map.AddMove(new(source, target, IsDirectory: false));
    }

    // A texture descriptor and the image beside it are one asset: each moves with the other, and a rename
    // renames an image that shares the descriptor's name.
    private static void AddTextureCompanions(AuthoredReferenceIndex index, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        var folder = source.Parent!;
        var targetFolder = target.Parent!;
        var stem = RelocationPaths.GetDisplayName(source.Name);
        var newStem = RelocationPaths.GetDisplayName(target.Name);
        FileSystemPath Companion(FileSystemPath file)
            => targetFolder.Combine(string.Equals(RelocationPaths.GetDisplayName(file.Name), stem, FileSystemPath.NameComparison)
                ? newStem + RelocationPaths.GetExtension(file.Name)
                : file.Name);

        if (source.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.OrdinalIgnoreCase))
        {
            var content = File.ReadAllBytes(source.FullPath);
            foreach (var image in AuthoredReferenceFormat.ReadTextureSources(source, content)
                .Where(image => image.Parent == folder && File.Exists(image.FullPath) && !map.IsMoved(image)))
            {
                AddFile(map, image, Companion(image));
            }

            return;
        }

        foreach (var descriptor in index.Descriptors.Where(descriptor => descriptor.Location.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.OrdinalIgnoreCase)
            && descriptor.Location.Parent == folder && !map.IsMoved(descriptor.Location)))
        {
            if (AuthoredReferenceFormat.ReadTextureSources(descriptor.Location, File.ReadAllBytes(descriptor.Location.FullPath)).Contains(source))
            {
                AddFile(map, descriptor.Location, Companion(descriptor.Location));
            }
        }
    }

    private static void ValidateGroup(string group)
    {
        var segments = group.Replace('\\', '/').Split('/');
        if (segments.Length == 0 || segments.Any(static segment => VirtualPath.ValidateSegment(segment, VirtualPathRuleSet.Authored) is not null))
        {
            throw new AssetRelocationException("Enter an output group such as 'Robot' or 'Vehicles/Car', without reserved characters.");
        }
    }

    private static void AddGroupFolder(ProjectContext project, RelocationMap map, List<string> folders, AssetPath from, AssetPath to)
    {
        if (from == to || map.Identities.Any(entry => entry.Source == from))
        {
            return;
        }

        var source = RelocationPaths.ToPhysical(project, from)!;
        var target = RelocationPaths.ToPhysical(project, to)!;
        if (source != target && (Directory.Exists(target.FullPath) || File.Exists(target.FullPath)))
        {
            throw new AssetRelocationException($"'{to}' already exists. Choose an output group that is not in use.");
        }

        if (Directory.Exists(source.FullPath) && !map.IsMoved(source))
        {
            map.AddMove(new(source, target, IsDirectory: true));
        }

        map.AddIdentity(from, to, isFolder: true);
        folders.Add(from + " → " + to);
    }

    // A real folder inside a type folder that is, or holds, an import's output group moves the matching
    // folder under the other two type folders too, so the group stays one unit.
    private static void ExpandImportGroups(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, List<string> folders, List<AuthoredReferenceIndex.ImportSource> moving)
    {
        foreach (var move in map.Moves.Where(static move => move.IsDirectory).ToArray())
        {
            var from = RelocationPaths.ToVirtual(project, move.Source)!;
            var to = RelocationPaths.ToVirtual(project, move.Target)!;
            foreach (var import in index.Imports.Where(import => !moving.Contains(import)))
            {
                foreach (var type in RelocationPaths.ImportTypeFolders)
                {
                    var typeFolder = AssetPath.Create(import.Settings.MountPoint, type);
                    if (!import.GetGroupFolder(type).IsSameOrInside(from) || from == typeFolder || !from.IsSameOrInside(typeFolder))
                    {
                        continue;
                    }

                    if (to == typeFolder || !to.IsSameOrInside(typeFolder))
                    {
                        throw new AssetRelocationException($"'{from}' holds the output group of '{import.PrimaryVirtualPath.Name}'. Output groups stay in '{typeFolder}'.");
                    }

                    var oldPart = from.GetPathBelow(typeFolder);
                    var newPart = to.GetPathBelow(typeFolder);
                    moving.Add(import);
                    folders.Add(from + " → " + to);
                    map.AddIdentity(from, to, isFolder: true);
                    foreach (var sibling in RelocationPaths.ImportTypeFolders.Where(sibling => !string.Equals(sibling, type, StringComparison.Ordinal)))
                    {
                        var siblingFolder = AssetPath.Create(import.Settings.MountPoint, sibling);
                        AddGroupFolder(project, map, folders, siblingFolder.Combine(oldPart), siblingFolder.Combine(newPart));
                    }

                    break;
                }
            }
        }
    }

    private static void CheckGroupOverlaps(AuthoredReferenceIndex index, RelocationMap map, List<AuthoredReferenceIndex.ImportSource> moving)
    {
        foreach (var import in moving)
        {
            var group = import.GetGroupFolder(RelocationPaths.ImportTypeFolders[0]);
            var mapped = map.MapIdentity(group) ?? group;
            foreach (var other in index.Imports.Where(other => !moving.Contains(other)))
            {
                var existing = other.GetGroupFolder(RelocationPaths.ImportTypeFolders[0]);
                if (mapped.IsSameOrInside(existing) || existing.IsSameOrInside(mapped))
                {
                    throw new AssetRelocationException($"The output group would overlap the group of '{other.PrimaryVirtualPath.Name}'. Choose a separate group.");
                }
            }
        }
    }

    private static async Task<IReadOnlyList<RelocationEdit>> PlanEditsAsync(
        ProjectContext project,
        AuthoredReferenceIndex index,
        RelocationMap map,
        IAtomicFileStore files,
        CancellationToken cancellationToken)
    {
        var edits = new List<RelocationEdit>();
        foreach (var descriptor in index.Descriptors)
        {
            var snapshot = await files.ReadAsync(descriptor.Location.FullPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            if (AuthoredReferenceFormat.RewriteDescriptor(descriptor.Location, content, map, index.Mounts) is { } rewritten)
            {
                edits.Add(new(descriptor.Location, map.MapPath(descriptor.Location), snapshot.Version, content, rewritten));
            }
        }

        foreach (var import in index.Imports)
        {
            var snapshot = await files.ReadAsync(import.SidecarPath.FullPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            byte[]? rewritten;
            try
            {
                rewritten = AuthoredReferenceFormat.RewriteImportSettings(project.ProjectRoot.FullPath, content, map);
            }
            catch (InvalidDataException error)
            {
                throw new AssetRelocationException($"The move would separate '{import.PrimaryVirtualPath.Name}' from its files. Move the model's folder instead.", error);
            }

            if (rewritten is not null)
            {
                edits.Add(new(import.SidecarPath, map.MapPath(import.SidecarPath), snapshot.Version, content, rewritten));
            }
        }

        return edits;
    }
}
