// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.Projects;

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
        var previousGroups = new Dictionary<string, string>(StringComparer.Ordinal);
        var groupFolders = new List<string>();
        var moving = new List<AuthoredReferenceIndex.ImportSource>();
        foreach (var move in request.Moves.Where(static move => !string.Equals(move.SourcePath, move.TargetPath, StringComparison.Ordinal)))
        {
            AddMove(project, index, map, move);
        }

        foreach (var move in request.GroupMoves)
        {
            var import = index.Imports.FirstOrDefault(item => string.Equals(item.PrimaryVirtualPath, move.SourcePath, StringComparison.OrdinalIgnoreCase))
                ?? throw new AssetRelocationException($"'{move.SourcePath}' is not an imported model with saved import settings.");
            ValidateGroup(move.Group);
            previousGroups[move.SourcePath] = import.Settings.OutputDirectory;
            moving.Add(import);
            foreach (var type in RelocationPaths.ImportTypeFolders)
            {
                AddGroupFolder(project, map, groupFolders, import.GetGroupFolder(type), RelocationPaths.Combine("/" + import.Settings.MountPoint + "/" + type, move.Group));
            }
        }

        ExpandImportGroups(project, index, map, groupFolders, moving);
        CheckGroupOverlaps(index, map, moving);
        foreach (var move in map.Moves)
        {
            var source = RelocationPaths.ToVirtual(project, move.Source)!;
            var target = RelocationPaths.ToVirtual(project, move.Target)!;
            map.AddIdentity(move.IsDirectory ? source : RelocationPaths.ToIdentity(source), move.IsDirectory ? target : RelocationPaths.ToIdentity(target), move.IsDirectory);
        }

        var edits = await PlanEditsAsync(project, index, map, files, cancellationToken).ConfigureAwait(false);
        var referrers = edits.Where(edit => !map.IsMoved(edit.OriginalPath))
            .Select(edit => RelocationPaths.ToVirtual(project, edit.OriginalPath)!)
            .Order(StringComparer.OrdinalIgnoreCase).ToArray();
        return new(request, map, edits, referrers, [.. groupFolders.Distinct(StringComparer.OrdinalIgnoreCase)], previousGroups);
    }

    private static void AddMove(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, AssetRelocationMove move)
    {
        var (source, target, isDirectory) = Resolve(project, move);
        if (map.Moves.Any(existing => IsSameOrInside(source, existing.Source) || IsSameOrInside(existing.Source, source)))
        {
            throw new AssetRelocationException("Select either a folder or the items inside it, not both.");
        }

        var name = RelocationPaths.GetName(move.SourcePath);
        if (!isDirectory && RelocationPaths.IsEditorScene(project, source))
        {
            throw new AssetRelocationException($"Scenes stay in {Constants.ContentFolderName}/{Constants.ScenesFolderName}; rename '{RelocationPaths.GetDisplayName(name)}' with Rename on the scene.");
        }

        if (isDirectory ? Directory.EnumerateFiles(source, "*" + Constants.SceneFileExtension, SearchOption.AllDirectories).Any() && ContainsEditorScenesTarget(project, target, source)
            : RelocationPaths.IsEditorScene(project, target))
        {
            throw new AssetRelocationException($"Scenes are created with New Scene; a scene file cannot be moved into {Constants.ContentFolderName}/{Constants.ScenesFolderName}.");
        }

        foreach (var import in index.Imports)
        {
            var bundle = Path.GetFullPath(Path.Combine(project.ProjectRoot, import.Settings.BundleRoot));
            var model = RelocationPaths.GetName(import.PrimaryVirtualPath);
            if (isDirectory)
            {
                if (source.StartsWith(bundle + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                {
                    throw new AssetRelocationException($"'{name}' is part of the imported model '{model}'. Move the model's folder instead.");
                }

                continue;
            }

            if (string.Equals(source, import.SidecarPath, StringComparison.OrdinalIgnoreCase))
            {
                throw new AssetRelocationException($"'{name}' holds the import settings of '{model}'. Rename the model instead.");
            }

            if (string.Equals(source, import.PrimaryPath, StringComparison.OrdinalIgnoreCase))
            {
                if (!string.Equals(Path.GetDirectoryName(source), Path.GetDirectoryName(target), StringComparison.OrdinalIgnoreCase))
                {
                    throw new AssetRelocationException($"'{model}' references its other files by relative path. Move the model's folder to move it.");
                }

                AddFile(map, import.SidecarPath, target + Import.NativeSceneImportSettings.SidecarSuffix);
            }
            else if (import.Settings.Files.Any(file => string.Equals(import.Settings.ResolveFile(project.ProjectRoot, file), source, StringComparison.OrdinalIgnoreCase)))
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

    private static (string source, string target, bool isDirectory) Resolve(ProjectContext project, AssetRelocationMove move)
    {
        var segments = RequireMovableSegments(project, move);
        if (RelocationPaths.ToPhysical(project, move.SourcePath) is not { } source || RelocationPaths.ToPhysical(project, move.TargetPath) is not { } target)
        {
            throw new AssetRelocationException($"'{move.SourcePath}' cannot be moved to '{move.TargetPath}'.");
        }

        var isDirectory = Directory.Exists(source);
        if (!isDirectory && !File.Exists(source))
        {
            throw new AssetRelocationException($"'{move.SourcePath}' no longer exists; it was deleted or moved outside the Content Browser.");
        }

        RequireValidTarget(move, segments, source, target, isDirectory);
        return (source, target, isDirectory);
    }

    private static string[] RequireMovableSegments(ProjectContext project, AssetRelocationMove move)
    {
        if (RelocationPaths.FindWritableMount(project, move.SourcePath) is null)
        {
            throw new AssetRelocationException($"'{move.SourcePath}' is read-only. Only project content can be renamed or moved.");
        }

        if (RelocationPaths.FindWritableMount(project, move.TargetPath) is null)
        {
            throw new AssetRelocationException("Choose a destination under Content or another authoring folder.");
        }

        var segments = move.SourcePath.Trim('/').Split('/');
        return segments.Length == 1 ? throw new AssetRelocationException("A content mount is renamed in Mounts, not here.") : segments;
    }

    private static void RequireValidTarget(AssetRelocationMove move, string[] segments, string source, string target, bool isDirectory)
    {
        var name = RelocationPaths.GetName(move.TargetPath);
        if (RelocationPaths.ValidateName(name) is { } invalid)
        {
            throw new AssetRelocationException(invalid);
        }

        if (isDirectory && segments.Length == 2 && RelocationPaths.ImportTypeFolders.Contains(segments[1], StringComparer.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException($"'{segments[1]}' is one of the importer's fixed folders and cannot be renamed or moved.");
        }

        if (isDirectory && RelocationPaths.IsSameOrInside(move.TargetPath, move.SourcePath))
        {
            throw new AssetRelocationException("A folder cannot be moved into itself.");
        }

        if (!isDirectory && !string.Equals(RelocationPaths.GetExtension(RelocationPaths.GetName(move.SourcePath)), RelocationPaths.GetExtension(name), StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException("A rename keeps the asset's type; change only its name.");
        }

        var caseOnly = string.Equals(source, target, StringComparison.OrdinalIgnoreCase);
        if (!caseOnly && (File.Exists(target) || Directory.Exists(target)))
        {
            throw new AssetRelocationException($"'{name}' already exists in '{RelocationPaths.GetParent(move.TargetPath)}'.");
        }

        if (!Directory.Exists(Path.GetDirectoryName(target)))
        {
            throw new AssetRelocationException($"The destination folder '{RelocationPaths.GetParent(move.TargetPath)}' does not exist.");
        }
    }

    private static bool IsSameOrInside(string path, string folder)
        => string.Equals(path, folder, StringComparison.OrdinalIgnoreCase) || path.StartsWith(folder + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    private static bool ContainsEditorScenesTarget(ProjectContext project, string target, string source)
    {
        var scenes = RelocationPaths.GetEditorScenesFolder(project);
        return Directory.EnumerateFiles(source, "*" + Constants.SceneFileExtension, SearchOption.AllDirectories)
            .Any(file => string.Equals(Path.GetDirectoryName(target + file[source.Length..]), scenes, StringComparison.OrdinalIgnoreCase));
    }

    private static void AddFile(RelocationMap map, string source, string target)
    {
        if (File.Exists(target) && !string.Equals(source, target, StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException($"'{Path.GetFileName(target)}' already exists next to the destination.");
        }

        map.AddMove(new(source, target, IsDirectory: false));
    }

    // A texture descriptor and the image beside it are one asset: each moves with the other, and a rename
    // renames an image that shares the descriptor's name.
    private static void AddTextureCompanions(AuthoredReferenceIndex index, RelocationMap map, string source, string target)
    {
        var folder = Path.GetDirectoryName(source)!;
        var targetFolder = Path.GetDirectoryName(target)!;
        var stem = RelocationPaths.GetDisplayName(Path.GetFileName(source));
        var newStem = RelocationPaths.GetDisplayName(Path.GetFileName(target));
        string Companion(string file)
        {
            var name = Path.GetFileName(file);
            var companionStem = RelocationPaths.GetDisplayName(name);
            return Path.Combine(targetFolder, string.Equals(companionStem, stem, StringComparison.OrdinalIgnoreCase) ? newStem + RelocationPaths.GetExtension(name) : name);
        }

        if (source.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase))
        {
            var content = File.ReadAllBytes(source);
            foreach (var image in AuthoredReferenceFormat.ReadTextureSources(source, content)
                .Where(image => string.Equals(Path.GetDirectoryName(image), folder, StringComparison.OrdinalIgnoreCase) && File.Exists(image) && !map.IsMoved(image)))
            {
                AddFile(map, image, Companion(image));
            }

            return;
        }

        foreach (var descriptor in index.Descriptors.Where(descriptor => descriptor.Path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
            && string.Equals(Path.GetDirectoryName(descriptor.Path), folder, StringComparison.OrdinalIgnoreCase) && !map.IsMoved(descriptor.Path)))
        {
            var sources = AuthoredReferenceFormat.ReadTextureSources(descriptor.Path, File.ReadAllBytes(descriptor.Path));
            if (sources.Contains(source, StringComparer.OrdinalIgnoreCase))
            {
                AddFile(map, descriptor.Path, Companion(descriptor.Path));
            }
        }
    }

    private static void ValidateGroup(string group)
    {
        var segments = group.Replace('\\', '/').Split('/');
        if (segments.Length == 0 || segments.Any(static segment => RelocationPaths.ValidateName(segment) is not null))
        {
            throw new AssetRelocationException("Enter an output group such as 'Robot' or 'Vehicles/Car', without reserved characters.");
        }
    }

    private static void AddGroupFolder(ProjectContext project, RelocationMap map, List<string> folders, string from, string to)
    {
        if (string.Equals(from, to, StringComparison.Ordinal) || map.Identities.Any(entry => string.Equals(entry.source, from, StringComparison.OrdinalIgnoreCase)))
        {
            return;
        }

        var source = RelocationPaths.ToPhysical(project, from)!;
        var target = RelocationPaths.ToPhysical(project, to)!;
        var caseOnly = string.Equals(source, target, StringComparison.OrdinalIgnoreCase);
        if (!caseOnly && (Directory.Exists(target) || File.Exists(target)))
        {
            throw new AssetRelocationException($"'{to}' already exists. Choose an output group that is not in use.");
        }

        if (Directory.Exists(source) && !map.IsMoved(source))
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
                    var typeFolder = "/" + import.Settings.MountPoint + "/" + type;
                    if (!RelocationPaths.IsSameOrInside(import.GetGroupFolder(type), from) || !from.StartsWith(typeFolder + "/", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    if (!to.StartsWith(typeFolder + "/", StringComparison.OrdinalIgnoreCase))
                    {
                        throw new AssetRelocationException($"'{from}' holds the output group of '{RelocationPaths.GetName(import.PrimaryVirtualPath)}'. Output groups stay in '{typeFolder}'.");
                    }

                    var oldPart = from[(typeFolder.Length + 1)..];
                    var newPart = to[(typeFolder.Length + 1)..];
                    moving.Add(import);
                    folders.Add(from + " → " + to);
                    map.AddIdentity(from, to, isFolder: true);
                    foreach (var sibling in RelocationPaths.ImportTypeFolders.Where(sibling => !string.Equals(sibling, type, StringComparison.Ordinal)))
                    {
                        var siblingFolder = "/" + import.Settings.MountPoint + "/" + sibling;
                        AddGroupFolder(project, map, folders, siblingFolder + "/" + oldPart, siblingFolder + "/" + newPart);
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
                if (RelocationPaths.IsSameOrInside(mapped, existing) || RelocationPaths.IsSameOrInside(existing, mapped))
                {
                    throw new AssetRelocationException($"The output group would overlap the group of '{RelocationPaths.GetName(other.PrimaryVirtualPath)}'. Choose a separate group.");
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
            var snapshot = await files.ReadAsync(descriptor.Path, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            if (AuthoredReferenceFormat.RewriteDescriptor(descriptor.Path, content, map, index.Mounts) is { } rewritten)
            {
                edits.Add(new(descriptor.Path, map.MapPath(descriptor.Path), snapshot.Version, content, rewritten));
            }
        }

        foreach (var import in index.Imports)
        {
            var snapshot = await files.ReadAsync(import.SidecarPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            byte[]? rewritten;
            try
            {
                rewritten = AuthoredReferenceFormat.RewriteImportSettings(project.ProjectRoot, content, map);
            }
            catch (InvalidDataException error)
            {
                throw new AssetRelocationException($"The move would separate '{RelocationPaths.GetName(import.PrimaryVirtualPath)}' from its files. Move the model's folder instead.", error);
            }

            if (rewritten is not null)
            {
                edits.Add(new(import.SidecarPath, map.MapPath(import.SidecarPath), snapshot.Version, content, rewritten));
            }
        }

        return edits;
    }
}
